import Foundation
@main struct PreviewGeometryTest {
 static func main() {
  let points = [CGPoint(x:0,y:0),CGPoint(x:1,y:0),CGPoint(x:1,y:1),CGPoint(x:0,y:1)]
  let portrait = previewPoints(points,image:CGSize(width:1080,height:1920),view:CGSize(width:300,height:500))
  assert(abs(portrait[0].x)<0.001 && abs(portrait[0].y+16.6666667)<0.001)
  assert(abs(portrait[2].x-300)<0.001 && abs(portrait[2].y-516.6666667)<0.001)
  let landscape = previewPoints(points,image:CGSize(width:1920,height:1080),view:CGSize(width:300,height:500))
  assert(landscape[0].x < 0 && landscape[0].y == 0)
  assert(previewPoints(points,image:CGSize(width:0,height:0),view:CGSize(width:300,height:500)).isEmpty)
  var tracker=PreviewOutlineTracker()
  let size=CGSize(width:540,height:960)
  tracker.observe(points,image:size,decoded:true,at:0)
  assert(tracker.current(at:0)==nil)
  tracker.observe(points,image:size,decoded:true,at:0.1)
  assert(tracker.current(at:0.1)?.decoded==true)
  tracker.observe([],image:CGSize(width:0,height:0),decoded:false,at:0.2)
  assert(tracker.current(at:0.2) != nil)
  assert(tracker.current(at:0.4)?.held==true && tracker.current(at:0.4)?.decoded==false)
  assert(tracker.current(at:0.71)==nil)
  tracker.reset()
  tracker.observe(points,image:size,decoded:true,at:1)
  tracker.observe(points,image:size,decoded:true,at:1.1)
  let jitter=points.map { CGPoint(x:$0.x+0.02,y:$0.y) }
  tracker.observe(jitter,image:size,decoded:false,at:1.2)
  let smoothed=tracker.current(at:1.2)!.points[0].x
  assert(smoothed>0 && smoothed<0.02)
  let jump=points.map { CGPoint(x:$0.x+0.3,y:$0.y) }
  tracker.observe(jump,image:size,decoded:false,at:1.3)
  assert(tracker.current(at:1.3)!.points[0].x<0.03)
  tracker.observe(jump,image:size,decoded:false,at:1.4)
  assert(tracker.current(at:1.4)!.points[0].x==0.3)
  assert(tracker.current(at:2.01)==nil)
  tracker.reset()
  tracker.observe(points,image:size,decoded:false,at:3)
  tracker.observe(points,image:size,decoded:false,at:4)
  assert(tracker.current(at:4)==nil)
  tracker.observe(points,image:size,decoded:false,at:4.1)
  assert(tracker.current(at:4.1) != nil)
  tracker.reset();assert(tracker.current(at:4.2)==nil)
  print("PASS: short loss is held/amber then expires; jitter is smoothed; one-off jumps rejected; reset and reacquisition.")
  print("PASS: four-corner aspect-fill mapping, portrait/landscape cropping and zero-size view.")
 }
}
