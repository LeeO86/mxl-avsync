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
  print("PASS: four-corner aspect-fill mapping, portrait/landscape cropping and zero-size view.")
 }
}
