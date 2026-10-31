import Foundation

// RFC 5905 four-timestamp exchange. No wall-clock adjustment and no media dependencies.
struct SntpPacket {
  static let epoch = 2_208_988_800.0
  struct Sample {
    let offsetMs: Double, uncertaintyMs: Double, networkRoundTripMs: Double
    let serverReceiveMs: Double, serverTransmitMs: Double
  }
  static func error(_ message: String) -> NSError { NSError(domain: "AVsync.Clock", code: 1, userInfo: [NSLocalizedDescriptionKey: message]) }
  static func request(unixMs: Double) -> Data {
    var bytes = [UInt8](repeating: 0, count: 48); bytes[0] = 0x23 // version 4, client
    let seconds = unixMs / 1000 + epoch
    let whole = floor(seconds), fraction = UInt32((seconds - whole) * 4_294_967_296)
    let sec = UInt32(truncatingIfNeeded: UInt64(whole))
    for i in 0..<4 { bytes[40+i] = UInt8(truncatingIfNeeded: sec >> (24-i*8)); bytes[44+i] = UInt8(truncatingIfNeeded: fraction >> (24-i*8)) }
    // A nonce below the timestamp resolution; prevents accepting a stale reply with a rounded T1.
    bytes[47] = UInt8.random(in: 0...255)
    return Data(bytes)
  }
  private static func u32(_ b: [UInt8], _ i: Int) -> UInt32 { b[i..<i+4].reduce(UInt32(0)) { ($0 << 8) | UInt32($1) } }
  private static func time(_ b: [UInt8], _ i: Int, near unixMs: Double) throws -> Double {
    guard b[i..<i+8].contains(where: { $0 != 0 }) else { throw error("NTP timestamp missing") }
    let raw = Double(u32(b,i)) + Double(u32(b,i+4)) / 4_294_967_296
    // Unfold the 2036 era rollover using the local calendar (within 68 years).
    let era = ((unixMs / 1000 + epoch - raw) / 4_294_967_296).rounded()
    return (raw + era * 4_294_967_296 - epoch) * 1000
  }
  static func decode(_ data: Data, request: Data, sentUnixMs t1: Double, receivedUnixMs t4: Double, elapsedMs: Double) throws -> Sample {
    let b = [UInt8](data), q = [UInt8](request)
    guard b.count >= 48, q.count == 48 else { throw error("Truncated NTP packet") }
    guard b[0] & 7 == 4, [3,4].contains(Int((b[0] >> 3) & 7)), b[0] >> 6 != 3 else { throw error("NTP server is not synchronized") }
    guard b[1] > 0, b[1] <= 15 else { throw error(b[1] == 0 ? "NTP server refused the request (kiss-of-death)" : "Invalid NTP stratum") }
    guard b[24..<32].elementsEqual(q[40..<48]) else { throw error("NTP reply does not match the request") }
    guard t1.isFinite, t4.isFinite, elapsedMs.isFinite, elapsedMs >= 0, elapsedMs <= 3000,
          abs(t4 - t1 - elapsedMs) <= 5 else { throw error("Device clock changed during the exchange") }
    let t2 = try time(b,32,near:t1), t3 = try time(b,40,near:t1)
    let processing = t3 - t2, network = elapsedMs - processing
    guard processing >= 0, network >= -0.1 else { throw error("Invalid NTP processing interval") }
    let rootDelay = Double(Int32(bitPattern:u32(b,4))) / 65_536 * 1000
    let dispersion = Double(u32(b,8)) / 65_536 * 1000
    let precision = pow(2, Double(Int8(bitPattern:b[3]))) * 1000
    let serverError = max(0,rootDelay)/2 + dispersion + max(0.001,precision)
    guard serverError.isFinite, serverError <= 1500 else { throw error("NTP server reports excessive uncertainty") }
    let offset = ((t2 - t1) + (t3 - t4)) / 2
    // Radius covers path asymmetry, server root distance, timestamp bracketing and local clock jitter.
    let uncertainty = max(0,network)/2 + serverError + abs(t4-t1-elapsedMs) + 1
    return Sample(offsetMs:offset, uncertaintyMs:uncertainty, networkRoundTripMs:max(0,network),serverReceiveMs:t2,serverTransmitMs:t3)
  }
}
