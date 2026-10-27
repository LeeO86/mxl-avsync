import Foundation

// Capture buffers and preview are both portrait. Match resizeAspectFill cropping.
func previewPoints(_ points: [CGPoint], image: CGSize, view: CGSize) -> [CGPoint] {
  guard image.width > 0, image.height > 0, view.width > 0, view.height > 0 else { return [] }
  let scale = max(view.width / image.width, view.height / image.height)
  let dx = (view.width - image.width * scale) / 2
  let dy = (view.height - image.height * scale) / 2
  return points.map { CGPoint(x: $0.x * image.width * scale + dx, y: $0.y * image.height * scale + dy) }
}
