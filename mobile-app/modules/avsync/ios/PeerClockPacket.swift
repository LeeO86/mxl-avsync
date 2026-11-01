import Foundation

// AVsync local clock protocol. This compares receiver time to the sender's
// frozen signal mapping; it is not a claim of UTC/PTP or I/O calibration.
struct PeerClockIdentity: Codable, Equatable {
  let sessionId: String, clockId: String, sourceId: UInt32
  let referenceUnixMs: Double, referenceMonotonicMs: Double, taiUtcSeconds: Double
}
struct PeerClockQuery: Codable {
  let version: Int, nonce: String, code: String, t1: Double
  init(code: String, t1: Double) { version=1;nonce=UUID().uuidString;self.code=code;self.t1=t1 }
}
struct PeerClockReply: Codable {
  let version: Int, nonce: String, t1: Double, t2: Double, t3: Double
  let identity: PeerClockIdentity
}
enum PeerClockPacket {
  static let serviceType="_avsync-peer._udp"
  static func error(_ message: String) -> NSError { NSError(domain:"AVsync.PeerClock",code:1,userInfo:[NSLocalizedDescriptionKey:message]) }
  static func validCode(_ value: String) -> Bool { value.range(of:"^[A-F0-9]{6}$",options:.regularExpression) != nil }
  static func serviceName(_ code: String) -> String { "AVsync-"+code }
  static func code(from service: String) -> String? {
    guard service.hasPrefix("AVsync-") else { return nil }
    let code=String(service.dropFirst(7));return validCode(code) ? code:nil
  }
  static func decode<T: Decodable>(_ type: T.Type, data: Data) throws -> T {
    guard data.count<=4096 else { throw error("Oversized peer-clock packet") }
    return try JSONDecoder().decode(type,from:data)
  }
  static func validate(_ reply: PeerClockReply, query: PeerClockQuery, receivedMonoMs t4: Double, deviceUnixMs: Double, localClockId: String) throws -> [String:Any] {
    let i=reply.identity
    guard reply.version==1, reply.nonce==query.nonce,reply.t1==query.t1,
          [query.t1,t4,reply.t2,reply.t3,i.referenceUnixMs,i.referenceMonotonicMs,i.taiUtcSeconds,deviceUnixMs].allSatisfy({$0.isFinite}),
          (1e12...4e12).contains(i.referenceUnixMs),i.referenceMonotonicMs>=0,
          !i.sessionId.isEmpty,!i.clockId.isEmpty,i.clockId != localClockId,(0...100).contains(i.taiUtcSeconds) else {
      throw error("Peer reply has invalid identity, time or request origin")
    }
    let elapsed=t4-query.t1, processing=reply.t3-reply.t2, rtt=elapsed-processing
    guard elapsed>=0,elapsed<=1500,processing>=0,processing<=1000,rtt>=(-0.1) else { throw error("Invalid peer-clock exchange interval") }
    let offset=((reply.t2-query.t1)+(reply.t3-t4))/2
    let unix=i.referenceUnixMs+(t4+offset-i.referenceMonotonicMs)
    let radius=max(0,rtt)/2+1 // asymmetry + local/network callback stamping allowance
    return ["source":"peer","referenceUnixMs":unix,"referenceMonotonicMs":t4,
      "deviceUnixMs":deviceUnixMs,"offsetMs":unix-deviceUnixMs,"uncertaintyMs":radius,
      "roundTripMs":max(0,rtt),"clockId":localClockId,"timebase":"ios-host",
      "peerSessionId":i.sessionId,"peerClockId":i.clockId,"peerSourceId":i.sourceId,
      "taiUtcSeconds":i.taiUtcSeconds,"authenticated":false,"estimated":true,
      "uncertaintyModel":"sender-relative network asymmetry + local stamping; physical I/O unknown"]
  }
}
