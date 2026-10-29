import Foundation

// Capture buffers and preview are both portrait. Match resizeAspectFill cropping.
func previewPoints(_ points: [CGPoint], image: CGSize, view: CGSize) -> [CGPoint] {
  guard image.width > 0, image.height > 0, view.width > 0, view.height > 0 else { return [] }
  let scale = max(view.width / image.width, view.height / image.height)
  let dx = (view.width - image.width * scale) / 2
  let dy = (view.height - image.height * scale) / 2
  return points.map { CGPoint(x: $0.x * image.width * scale + dx, y: $0.y * image.height * scale + dy) }
}

// Presentation only: this never feeds smoothed coordinates back to detection.
// Brief missed frames fade to amber; lost targets still expire promptly.
struct PreviewOutlineTracker {
  struct Outline {
    let points: [CGPoint]
    let image: CGSize
    let decoded: Bool
    let held: Bool
  }
  private var points: [CGPoint] = []
  private var image = CGSize(width:0,height:0)
  private var seenAt = -Double.infinity
  private var decodedAt = -Double.infinity
  private var candidate: [CGPoint] = []
  private var candidateAt = -Double.infinity
  private let lossLease = 0.6

  mutating func reset() { self = PreviewOutlineTracker() }
  private func distance(_ a: [CGPoint], _ b: [CGPoint]) -> Double {
    guard a.count == 4, b.count == 4 else { return .infinity }
    return sqrt(zip(a,b).reduce(0.0) { $0 + pow(Double($1.0.x-$1.1.x),2) + pow(Double($1.0.y-$1.1.y),2) } / 4)
  }
  mutating func observe(_ incoming: [CGPoint], image size: CGSize, decoded: Bool, at now: Double) {
    guard now.isFinite else { return }
    if !points.isEmpty && (now < seenAt || now-seenAt > lossLease) { reset() }
    guard incoming.count == 4, incoming.allSatisfy({ $0.x.isFinite && $0.y.isFinite }),
          size.width.isFinite, size.height.isFinite, size.width > 0, size.height > 0 else { return }
    let jump = !points.isEmpty && distance(points,incoming) > 0.12
    if points.isEmpty || jump {
      // Two nearby observations acquire/reacquire; ignore a single spurious quad.
      if candidate.isEmpty || now-candidateAt > 0.25 || distance(candidate,incoming) > 0.04 {
        candidate=incoming;candidateAt=now;return
      }
      points=incoming;decodedAt = -.infinity
    } else {
      let alpha = CGFloat(1-exp(-max(0,now-seenAt)/0.12))
      points=zip(points,incoming).map { pair in CGPoint(x:pair.0.x+alpha*(pair.1.x-pair.0.x),y:pair.0.y+alpha*(pair.1.y-pair.0.y)) }
    }
    candidate=[];image=size;seenAt=now
    if decoded { decodedAt=now }
  }
  func current(at now: Double) -> Outline? {
    guard points.count == 4, now >= seenAt, now-seenAt <= lossLease else { return nil }
    let held=now-seenAt > 0.18
    return Outline(points:points,image:image,decoded:!held && now-decodedAt <= 0.35,held:held)
  }
}
