import Foundation
import Network

final class PeerClockHost {
  let code: String
  let identity: PeerClockIdentity
  private let queue=DispatchQueue(label:"avsync.peer.host",qos:.userInitiated)
  private let lock=NSLock()
  private var listener: NWListener?
  private var connections=[UUID:NWConnection]()
  private var ready=false, stopped=false, failure: String?
  private var boundPort: UInt16=0
  private let now: ()->Double
  init(sourceId: UInt32, unixMs: Double, monotonicMs: Double, taiUtcSeconds: Double,
       clockId: String=PhoneClock.identifier, now: @escaping ()->Double=PhoneClock.nowMs) {
    code=String(UUID().uuidString.replacingOccurrences(of:"-",with:"").prefix(6))
    identity=PeerClockIdentity(sessionId:UUID().uuidString,clockId:clockId,sourceId:sourceId,
      referenceUnixMs:unixMs,referenceMonotonicMs:monotonicMs,taiUtcSeconds:taiUtcSeconds)
    self.now=now
  }
  func start(advertise: Bool=true) throws {
    guard listener==nil else { return }
    let server=try NWListener(using:.udp,on:.any);listener=server
    if advertise { server.service=NWListener.Service(name:PeerClockPacket.serviceName(code),type:PeerClockPacket.serviceType) }
    server.stateUpdateHandler={ [weak self,weak server] state in
      guard let self else { return };self.lock.lock();defer{self.lock.unlock()}
      switch state {
      case .ready:self.ready=true;self.boundPort=server?.port?.rawValue ?? 0
      case .failed(let error):self.failure=error.localizedDescription;self.ready=false
      case .cancelled:self.ready=false
      default:break
      }
    }
    server.newConnectionHandler={ [weak self] connection in
      guard let self else { connection.cancel();return }
      guard self.connections.count<16 else { connection.cancel();return }
      let id=UUID();self.connections[id]=connection;connection.start(queue:self.queue)
      self.receive(connection,id:id)
      self.queue.asyncAfter(deadline:.now()+3) { [weak self,weak connection] in connection?.cancel();self?.connections.removeValue(forKey:id) }
    }
    server.start(queue:queue)
  }
  private func receive(_ connection: NWConnection,id: UUID) {
    connection.receiveMessage { [weak self,weak connection] data,_,_,error in
      guard let self,let connection else { return }
      let t2=self.now()
      self.lock.lock();let active = !self.stopped;self.lock.unlock()
      guard active,error==nil,let data else { connection.cancel();self.connections.removeValue(forKey:id);return }
      do {
        let query=try PeerClockPacket.decode(PeerClockQuery.self,data:data)
        guard query.version==1,query.code==self.code,query.t1.isFinite,!query.nonce.isEmpty else { throw PeerClockPacket.error("Invalid pairing request") }
        let reply=PeerClockReply(version:1,nonce:query.nonce,t1:query.t1,t2:t2,t3:self.now(),identity:self.identity)
        connection.send(content:try JSONEncoder().encode(reply),completion:.contentProcessed { _ in })
      } catch { /* Invalid/unpaired datagrams receive no clock response. */ }
      self.receive(connection,id:id)
    }
  }
  func info() -> [String:Any] {
    lock.lock();defer{lock.unlock()}
    var result:[String:Any]=["code":code,"serviceName":PeerClockPacket.serviceName(code),"ready":ready && !stopped,
      "port":boundPort,"sourceId":identity.sourceId,"sessionId":identity.sessionId]
    if let failure { result["error"]=failure };return result
  }
  func stop() {
    lock.lock();stopped=true;ready=false;lock.unlock();listener?.cancel()
    queue.async { [weak self] in guard let self else{return};for c in self.connections.values{c.cancel()};self.connections.removeAll() }
  }
  deinit { listener?.cancel() }
}

final class PeerClockClient {
  private let queue=DispatchQueue(label:"avsync.peer.client")
  func synchronize(serviceName: String, code: String) throws -> [String:Any] {
    guard PeerClockPacket.code(from:serviceName)==code else { throw PeerClockPacket.error("Choose the sender with the matching six-character code") }
    let endpoint=NWEndpoint.service(name:serviceName,type:PeerClockPacket.serviceType,domain:"local.",interface:nil)
    return try synchronize(endpoint:endpoint,code:code,name:serviceName)
  }
  func synchronize(endpoint: NWEndpoint,code: String,name: String="Local sender",samples: Int=8) throws -> [String:Any] {
    guard PeerClockPacket.validCode(code),samples>=3,samples<=12 else { throw PeerClockPacket.error("Invalid peer-clock check") }
    var values=[[String:Any]](),lastError: Error?
    for n in 0..<samples {
      if n>0 { Thread.sleep(forTimeInterval:0.08) }
      do { values.append(try query(endpoint:endpoint,code:code)) } catch { lastError=error;if values.isEmpty && n>=1{break} }
    }
    guard values.count>=3,var best=values.min(by:{($0["uncertaintyMs"] as! Double)<($1["uncertaintyMs"] as! Double)}) else {throw lastError ?? PeerClockPacket.error("Sender unavailable or pairing code changed")}
    let session=best["peerSessionId"] as! String,source=best["peerSourceId"] as! UInt32
    let baseUnix=best["referenceUnixMs"] as! Double,baseMono=best["referenceMonotonicMs"] as! Double,radius=best["uncertaintyMs"] as! Double
    guard values.allSatisfy({ value in
      value["peerSessionId"] as? String==session && value["peerSourceId"] as? UInt32==source &&
        abs((value["referenceUnixMs"] as! Double)-baseUnix-((value["referenceMonotonicMs"] as! Double)-baseMono)) <= radius+(value["uncertaintyMs"] as! Double)+1
    }) else { throw PeerClockPacket.error("Sender restarted or clock samples disagree; synchronize again") }
    best["server"]=name;best["samples"]=values.count;return best
  }
  private func query(endpoint: NWEndpoint,code: String) throws -> [String:Any] {
    let connection=NWConnection(to:endpoint,using:.udp),ready=DispatchSemaphore(value:0),received=DispatchSemaphore(value:0),lock=NSLock()
    var didReady=false,reply: [String:Any]?,failure: Error?
    connection.stateUpdateHandler={ state in
      lock.lock();defer{lock.unlock()}
      guard !didReady else{return}
      switch state {case .ready:didReady=true;ready.signal();case .failed(let e):failure=e;didReady=true;ready.signal();default:break}
    }
    connection.start(queue:queue);defer{connection.cancel()}
    guard ready.wait(timeout:.now()+2) == .success else{throw PeerClockPacket.error("Sender discovery/network timed out")}
    lock.lock();let early=failure;lock.unlock();if let early{throw early}
    let query=PeerClockQuery(code:code,t1:PhoneClock.nowMs())
    connection.receiveMessage { data,_,_,error in
      let t4=PhoneClock.nowMs(),unix=Date().timeIntervalSince1970*1000
      lock.lock();defer{lock.unlock();received.signal()}
      do {if let error{throw error};guard let data else{throw PeerClockPacket.error("Empty sender reply")}
        reply=try PeerClockPacket.validate(PeerClockPacket.decode(PeerClockReply.self,data:data),query:query,receivedMonoMs:t4,deviceUnixMs:unix,localClockId:PhoneClock.identifier)
      }catch{failure=error}
    }
    connection.send(content:try JSONEncoder().encode(query),completion:.contentProcessed{ error in if let error{lock.lock();failure=error;lock.unlock();received.signal()} })
    guard received.wait(timeout:.now()+2) == .success else{throw PeerClockPacket.error("Sender did not reply; check Local Network permission and code")}
    lock.lock();defer{lock.unlock()};if let failure{throw failure};guard let reply else{throw PeerClockPacket.error("No peer clock reply")};return reply
  }
}

final class PeerClockDiscovery {
  private var browser: NWBrowser?
  private var found=Set<String>()
  func discover(seconds: Double=2.5) -> [String] {
    let queue=DispatchQueue(label:"avsync.peer.discovery"),done=DispatchSemaphore(value:0),lock=NSLock()
    let browser=NWBrowser(for:.bonjour(type:PeerClockPacket.serviceType,domain:"local."),using:.udp);self.browser=browser
    browser.browseResultsChangedHandler={ [weak self] results,_ in
      lock.lock();defer{lock.unlock()}
      self?.found=Set(results.compactMap { result in
        if case .service(let name,_,_,_)=result.endpoint,PeerClockPacket.code(from:name) != nil{return name};return nil
      })
    }
    browser.start(queue:queue);queue.asyncAfter(deadline:.now()+seconds){done.signal()};done.wait()
    lock.lock();let names=found.sorted();lock.unlock();browser.cancel();self.browser=nil;return names
  }
}
