import Foundation
import CoreMedia
import Network

// This UUID is process-local. A saved reference must never survive a process restart or reboot.
enum PhoneClock {
  static let identifier = UUID().uuidString
  static func nowMs() -> Double { CMTimeGetSeconds(CMClockGetTime(CMClockGetHostTimeClock())) * 1000 }
  static func now() -> [String: Any] { ["monotonicMs":nowMs(),"clockId":identifier,"timebase":"ios-host"] }
}

final class SntpClient {
  private let queue = DispatchQueue(label:"avsync.ntp.network")
  private func query(host: String, port: UInt16) throws -> [String: Any] {
    guard let endpointPort = NWEndpoint.Port(rawValue:port) else { throw SntpPacket.error("Invalid NTP port") }
    let connection = NWConnection(host:NWEndpoint.Host(host), port:endpointPort, using:.udp)
    let ready = DispatchSemaphore(value:0), received = DispatchSemaphore(value:0)
    let lock = NSLock()
    var connectionError: Error?, completed = false
    var result: [String: Any]?, failure: Error?
    connection.stateUpdateHandler = { state in
      lock.lock(); defer { lock.unlock() }
      if completed { return }
      switch state {
      case .ready: completed = true; ready.signal()
      case .failed(let error): connectionError = error; completed = true; ready.signal()
      case .waiting(let error): connectionError = error
      default: break
      }
    }
    connection.start(queue:queue)
    defer { connection.cancel() }
    guard ready.wait(timeout:.now()+3) == .success else { throw SntpPacket.error("NTP server unreachable (DNS/network timeout)") }
    lock.lock(); let readyError = connectionError; lock.unlock()
    if let readyError { throw readyError }
    let sentMono = PhoneClock.nowMs(), sentUnix = Date().timeIntervalSince1970 * 1000
    let request = SntpPacket.request(unixMs:sentUnix)
    connection.send(content:request, completion:.contentProcessed { error in
      if let error { lock.lock(); failure = error; lock.unlock(); received.signal() }
    })
    connection.receiveMessage { data, _, _, error in
      let receivedMono = PhoneClock.nowMs(), receivedUnix = Date().timeIntervalSince1970 * 1000
      lock.lock(); defer { lock.unlock(); received.signal() }
      do {
        if let error { throw error }
        guard let data else { throw SntpPacket.error("Empty NTP reply") }
        let sample = try SntpPacket.decode(data,request:request,sentUnixMs:sentUnix,receivedUnixMs:receivedUnix,elapsedMs:receivedMono-sentMono)
        result = ["source":"ntp", "server":host, "referenceUnixMs":receivedUnix+sample.offsetMs,
                  "referenceMonotonicMs":receivedMono,"deviceUnixMs":receivedUnix,
                  "offsetMs":sample.offsetMs,"uncertaintyMs":sample.uncertaintyMs,"roundTripMs":sample.networkRoundTripMs,
                  "clockId":PhoneClock.identifier,"timebase":"ios-host","authenticated":false,
                  "uncertaintyModel":"network-asymmetry + server-root-distance + local-stamping"]
      } catch { failure = error }
    }
    guard received.wait(timeout:.now()+3) == .success else { throw SntpPacket.error("NTP reply timed out") }
    lock.lock(); defer { lock.unlock() }
    if let failure { throw failure }
    guard let result else { throw SntpPacket.error("No valid NTP reply") }
    return result
  }
  // Called on a worker, never on the UI/network callback queue. Port is fixed at 123 in the module.
  func synchronize(host: String, port: UInt16 = 123, samples: Int = 4) throws -> [String: Any] {
    guard !host.isEmpty, host.count <= 253, (!host.contains(":") || host.filter({ $0 == ":" }).count >= 2), host.range(of:"^[A-Za-z0-9:.\\-]+$",options:.regularExpression) != nil else { throw SntpPacket.error("Enter an NTP hostname or IP address, without a URL or port") }
    var results = [[String: Any]](), lastError: Error?
    for i in 0..<samples {
      if i > 0 { Thread.sleep(forTimeInterval:1) }
      do { results.append(try query(host:host,port:port)) }
      catch { lastError = error; if error.localizedDescription.contains("kiss-of-death") { break } }
    }
    guard var best = results.min(by:{ ($0["uncertaintyMs"] as! Double) < ($1["uncertaintyMs"] as! Double) }) else { throw lastError ?? SntpPacket.error("NTP unavailable") }
    // Independent samples must agree with the selected interval; otherwise do not invent a clock lock.
    let centre = best["offsetMs"] as! Double, radius = best["uncertaintyMs"] as! Double
    guard results.allSatisfy({ abs(($0["offsetMs"] as! Double)-centre) <= ($0["uncertaintyMs"] as! Double)+radius+1 }) else { throw SntpPacket.error("NTP samples disagree; synchronization rejected") }
    best["samples"] = results.count
    best["sampleAgeMs"] = max(0,PhoneClock.nowMs()-(best["referenceMonotonicMs"] as! Double))
    return best
  }
}
