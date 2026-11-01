import Foundation
import Network

@main struct PeerClockTests {
  static func main() throws {
    let epoch=1_793_500_000_000.0
    let identity=PeerClockIdentity(sessionId:"session-a",clockId:"sender-clock",sourceId:42,referenceUnixMs:epoch,referenceMonotonicMs:1000,taiUtcSeconds:37)
    let q=PeerClockQuery(code:"ABCDEF",t1:1000)
    let reply=PeerClockReply(version:1,nonce:q.nonce,t1:q.t1,t2:1008,t3:1010,identity:identity)
    let sample=try PeerClockPacket.validate(reply,query:q,receivedMonoMs:1006,deviceUnixMs:epoch+6,localClockId:"receiver-clock")
    precondition(abs((sample["referenceUnixMs"] as! Double)-(epoch+12))<0.001)
    precondition(sample["uncertaintyMs"] as! Double==3 && sample["offsetMs"] as! Double==6)
    precondition(PeerClockPacket.code(from:"AVsync-ABCDEF")=="ABCDEF")
    precondition(PeerClockPacket.code(from:"Other-ABCDEF")==nil)
    for bad in [PeerClockReply(version:2,nonce:q.nonce,t1:q.t1,t2:1008,t3:1010,identity:identity),
                PeerClockReply(version:1,nonce:"old",t1:q.t1,t2:1008,t3:1010,identity:identity),
                PeerClockReply(version:1,nonce:q.nonce,t1:q.t1,t2:1010,t3:1008,identity:identity)] {
      do { _=try PeerClockPacket.validate(bad,query:q,receivedMonoMs:1006,deviceUnixMs:epoch,localClockId:"receiver-clock");preconditionFailure("Accepted invalid reply") } catch {}
    }
    do { _=try PeerClockPacket.validate(reply,query:q,receivedMonoMs:1006,deviceUnixMs:epoch,localClockId:identity.clockId);preconditionFailure("Accepted own clock") } catch {}
    do { let _:PeerClockQuery=try PeerClockPacket.decode(PeerClockQuery.self,data:Data(repeating:0,count:4097));preconditionFailure("Accepted oversized datagram") } catch {}
    let jumpLock=NSLock();var calls=0
    let jumping=PeerClockHost(sourceId:78,unixMs:Date().timeIntervalSince1970*1000,
      monotonicMs:PhoneClock.nowMs(),taiUtcSeconds:37,clockId:"jumping-sender",now:{
        jumpLock.lock();calls+=1;let step=calls>2 ? 40.0:0.0;jumpLock.unlock();return PhoneClock.nowMs()+step
      })
    try jumping.start(advertise:false);defer{jumping.stop()}
    for _ in 0..<100 {if jumping.info()["ready"] as? Bool==true{break};Thread.sleep(forTimeInterval:0.02)}
    let jumpPort=jumping.info()["port"] as! UInt16
    do {_=try PeerClockClient().synchronize(endpoint:.hostPort(host:"127.0.0.1",port:NWEndpoint.Port(rawValue:jumpPort)!),code:jumping.code,samples:4);preconditionFailure("Accepted disagreeing sender samples")} catch {
      precondition(error.localizedDescription.contains("disagree"))
    }
    let host=PeerClockHost(sourceId:77,unixMs:Date().timeIntervalSince1970*1000+25,
      monotonicMs:PhoneClock.nowMs()+125,taiUtcSeconds:37,clockId:"loopback-sender",now:{PhoneClock.nowMs()+125})
    try host.start(advertise:false);defer{host.stop()}
    for _ in 0..<100 { if host.info()["ready"] as? Bool==true {break};Thread.sleep(forTimeInterval:0.02) }
    guard let port=host.info()["port"] as? UInt16,port>0 else{throw PeerClockPacket.error("Loopback listener failed")}
    let value=try PeerClockClient().synchronize(endpoint:.hostPort(host:"127.0.0.1",port:NWEndpoint.Port(rawValue:port)!),code:host.code,samples:4)
    precondition(value["peerSourceId"] as! UInt32==77)
    precondition(abs((value["offsetMs"] as! Double)-25)<(value["uncertaintyMs"] as! Double)+2)
    precondition(value["samples"] as! Int==4 && value["peerSessionId"] as! String==host.identity.sessionId)
    print("PASS: peer packet origin/identity/interval validation and actual UDP four-timestamp exchange with independent +125-ms host time and +25-ms sender mapping")
  }
}
