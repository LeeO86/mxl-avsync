import Foundation
@main struct CaptureProfileTests {
  static func main() {
    let formats=[CaptureFormatRange(format:0,width:3840,height:2160,minFps:1,maxFps:60),CaptureFormatRange(format:1,width:1920,height:1080,minFps:30,maxFps:240)]
    for cap in [60.0,120.0,240.0] { let choice=chooseCaptureFormat(formats,cap:cap)!;precondition(choice.range==1 && choice.fps==cap) }
    precondition(chooseCaptureFormat(formats,cap:0)!.fps==240)
    let limited=[CaptureFormatRange(format:0,width:1920,height:1080,minFps:30,maxFps:60)]
    precondition(chooseCaptureFormat(limited,cap:120)!.fps==60)
    precondition(chooseCaptureFormat(formats,cap:90)==nil)
    let t=CaptureTelemetry();t.reset(fps:120)
    for i in 0...120 { if !(40...43).contains(i) { t.frame(at:Double(i)*1000/120) } }
    t.drop(reason:"late");t.processing("poll",ms:27)
    let stats=t.snapshot(events:[["seq":7,"videoTimeMs":350.0]])
    precondition(stats["deliveredFrames"] as! Int==117)
    precondition(stats["estimatedMissingFramesFromPts"] as! Int==4)
    precondition(stats["dropCallbacks"] as! Int==1)
    precondition(abs(stats["maxFrameGapMs"] as! Double-41.6666667)<0.01)
    let gap=(stats["eventFrameGaps"] as! [[String:Any]])[0]["frameGapMs"] as! Double
    precondition(gap>40)
    precondition(stats["pollProcessingMaxMs"] as! Double==27)
    t.reset(fps:60);precondition(t.snapshot()["deliveredFrames"] as! Int==0)
    for i in 0..<1000 { t.processing("qrVision",ms:Double(i));t.videoPath(4) }
    let profiled=t.snapshot()
    precondition(profiled["qrVisionProcessingMaxMs"] as! Double==999)
    precondition(profiled["qrVisionProcessingP95Ms"] as! Double>980)
    precondition((profiled["videoPaths"] as! [String:Int])["warmupSkipped"]==1000)
    t.reset(fps:240);precondition((t.snapshot()["videoPaths"] as! [String:Int]).isEmpty)
    print("Capture profile/telemetry passed: stable raster, capped rates, real gap at pulse, independent drop/processing counters, reset")
  }
}
