import Foundation

// Pick a rate cap without silently switching to a larger, more expensive raster.
// Prefer the fastest camera's existing format for comparable 60/120/240 runs.
struct CaptureFormatRange {
  let format: Int, width: Int, height: Int
  let minFps: Double, maxFps: Double
}
struct CaptureFormatChoice {
  let range: Int, fps: Double
}
func chooseCaptureFormat(_ formats: [CaptureFormatRange], cap: Double) -> CaptureFormatChoice? {
  guard [0,60,120,240].contains(cap), !formats.isEmpty else { return nil }
  let fastest = formats.indices.max { a,b in
    if formats[a].maxFps != formats[b].maxFps { return formats[a].maxFps < formats[b].maxFps }
    return formats[a].width * formats[a].height < formats[b].width * formats[b].height
  }!
  if cap == 0 { return CaptureFormatChoice(range:fastest,fps:formats[fastest].maxFps) }
  let viable = formats.indices.filter { min(cap,formats[$0].maxFps) >= formats[$0].minFps }
  guard let rate = viable.map({ min(cap,formats[$0].maxFps) }).max() else { return nil }
  let candidates = viable.filter { formats[$0].minFps <= rate && formats[$0].maxFps >= rate }
  let base = formats[fastest]
  let chosen = candidates.min { a,b in
    let x=formats[a],y=formats[b]
    if (x.format == base.format) != (y.format == base.format) { return x.format == base.format }
    let dx=abs(x.width-base.width)+abs(x.height-base.height),dy=abs(y.width-base.width)+abs(y.height-base.height)
    if dx != dy { return dx < dy }
    if x.width*x.height != y.width*y.height { return x.width*x.height < y.width*y.height }
    return x.maxFps > y.maxFps
  }!
  return CaptureFormatChoice(range:chosen,fps:rate)
}

// Bounded, samples-queue-only telemetry. PTS gaps and didDrop callbacks describe
// overlapping evidence: they must not be summed into a fabricated lost-frame total.
final class CaptureTelemetry {
  private struct Frame { let time: Double, gap: Double }
  private var frames = [Frame](), start = 0
  private var timings = [String:[Double]]()
  private var delivered = 0, drops = [String:Int](), inferredMissing = 0
  private var previous: Double?
  private var configuredFps = 120.0
  func reset(fps: Double) { frames=[];start=0;timings=[:];delivered=0;drops=[:];inferredMissing=0;previous=nil;configuredFps=fps }
  func frame(at time: Double) {
    guard time.isFinite else { return }
    if let previous, time <= previous { self.previous=nil;frames=[];start=0 }
    let gap=previous.map { time-$0 } ?? 0
    if gap > 0 && configuredFps > 0 { inferredMissing += max(0,Int((gap*configuredFps/1000).rounded())-1) }
    previous=time;delivered+=1;frames.append(Frame(time:time,gap:gap))
    while start < frames.count && frames[start].time < time-6000 { start+=1 }
    if start > 2048 { frames.removeFirst(start);start=0 }
  }
  func drop(reason: String) { drops[reason,default:0]+=1 }
  func processing(_ kind: String, ms: Double) {
    guard ms.isFinite && ms >= 0 else { return }
    var values=timings[kind,default:[]];values.append(ms)
    if values.count > 128 { values.removeFirst(values.count-128) }
    timings[kind]=values
  }
  private func p95(_ values: [Double]) -> Double { let v=values.sorted();return v.isEmpty ? 0 : v[min(v.count-1,Int(Double(v.count-1)*0.95))] }
  func snapshot(events: [[String:Any]] = []) -> [String:Any] {
    let current=Array(frames.dropFirst(start)),gaps=current.dropFirst().map(\.gap)
    let duration=current.count>1 ? current.last!.time-current.first!.time : 0
    var result: [String:Any] = ["deliveredFrames":delivered,"configuredFps":configuredFps,
      "deliveredFps":duration>0 ? Double(current.count-1)*1000/duration : 0,
      "windowMs":duration,"maxFrameGapMs":gaps.max() ?? 0,"p95FrameGapMs":p95(gaps),
      "dropCallbacks":drops.values.reduce(0,+),"dropReasons":drops,"estimatedMissingFramesFromPts":inferredMissing]
    for kind in ["video","audio","poll"] { let values=timings[kind,default:[]];result[kind+"ProcessingP95Ms"]=p95(values);result[kind+"ProcessingMaxMs"]=values.max() ?? 0 }
    var edges=[[String:Any]]()
    for event in events.suffix(20) {
      guard let time=event["videoTimeMs"] as? Double, let first=current.first, time>=first.time,
            let frame=current.first(where:{$0.time>=time}) else { continue }
      edges.append(["seq":event["seq"] ?? "unknown","eventTimeMs":time,"beforeFrameMs":frame.time-frame.gap,"afterFrameMs":frame.time,"frameGapMs":frame.gap])
    }
    result["eventFrameGaps"]=edges
    return result
  }
}
