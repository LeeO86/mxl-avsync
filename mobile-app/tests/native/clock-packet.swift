import Foundation
@main struct ClockPacketTests {
  static func main() throws {
    let epoch = 1_793_450_000_000.0
    let request = SntpPacket.request(unixMs:epoch)
    func packet(t2: Double = 50,t3: Double = 52) -> Data {
      var b=[UInt8](repeating:0,count:48);b[0]=0x24;b[1]=2;b[3]=UInt8(bitPattern:-20)
      b.replaceSubrange(24..<32,with:request[40..<48])
      b.replaceSubrange(32..<40,with:SntpPacket.request(unixMs:epoch+t2)[40..<48])
      b.replaceSubrange(40..<48,with:SntpPacket.request(unixMs:epoch+t3)[40..<48])
      return Data(b)
    }
    let valid=packet()
    func decode(_ b: Data, t4: Double = 100, elapsed: Double = 100) throws -> SntpPacket.Sample { try SntpPacket.decode(b,request:request,sentUnixMs:epoch,receivedUnixMs:epoch+t4,elapsedMs:elapsed) }
    let s=try decode(valid)
    precondition(abs(s.offsetMs-1)<0.01 && s.uncertaintyMs>=50 && abs(s.networkRoundTripMs-98)<0.01)
    func reject(_ b: Data, t4: Double = 100, elapsed: Double = 100) {
      do { _ = try decode(b,t4:t4,elapsed:elapsed); fatalError("Accepted invalid NTP packet") } catch {}
    }
    reject(Data(valid.prefix(40)))
    for (index,value) in [(0,UInt8(0xe4)),(0,UInt8(0x23)),(1,UInt8(0)),(1,UInt8(16)),(24,UInt8(255))] {
      var b=valid;b[index]=value;reject(b)
    }
    reject(packet(t2:60,t3:50));reject(packet(t2:0,t3:200))
    reject(valid,t4:500,elapsed:100)
    var zero=valid;zero.replaceSubrange(32..<40,with:repeatElement(UInt8(0),count:8));reject(zero)
    var excessive=valid;excessive[8]=0x01;reject(excessive)
    // A future epoch after the NTP era rollover still decodes relative to the local calendar.
    let future=2_100_000_000_000.0, q=SntpPacket.request(unixMs:future)
    var b=[UInt8](repeating:0,count:48);b[0]=0x24;b[1]=1;b[3]=UInt8(bitPattern:-20)
    b.replaceSubrange(24..<32,with:q[40..<48]);b.replaceSubrange(32..<40,with:SntpPacket.request(unixMs:future+10)[40..<48]);b.replaceSubrange(40..<48,with:SntpPacket.request(unixMs:future+11)[40..<48])
    let era=try SntpPacket.decode(Data(b),request:q,sentUnixMs:future,receivedUnixMs:future+20,elapsedMs:20)
    precondition(abs(era.offsetMs-0.5)<0.01)
    print("SNTP packet tests passed: offset/asymmetry, origin, LI/mode/stratum, timestamp order, wall step, root error, era rollover")
  }
}
