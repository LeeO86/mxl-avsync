import Foundation
import UIKit

@main struct ImageStoreTest {
 static func main() throws {
  let format = UIGraphicsImageRendererFormat.default();format.scale = 1
  let image = UIGraphicsImageRenderer(size: CGSize(width: 1600, height: 800), format: format).image { _ in
    UIColor.orange.setFill();UIRectFill(CGRect(x: 0,y: 0,width: 1600,height: 800))
  }
  let saved = try GeneratorImageStore.save(image)
  let id = saved["id"] as! String
  defer { GeneratorImageStore.remove(id: id) }
  precondition(UUID(uuidString: id) != nil)
  let restored = try GeneratorImageStore.load(id: id)!
  precondition(restored.size == CGSize(width: 512,height: 256))
  let info = GeneratorImageStore.info(id: id)!
  precondition(info["id"] as? String == id && (info["uri"] as! String).hasPrefix("file://"))
  let empty = try GeneratorImageStore.load(id: "")
  precondition(empty == nil)
  for invalid in ["../outside","file:///tmp/photo.png","https://example.com/photo.png"] {
    do { _ = try GeneratorImageStore.url(id: invalid);fatalError("unscoped image accepted") } catch { }
    GeneratorImageStore.remove(id: invalid) // Must never escape the app directory.
  }
  let other = try GeneratorImageStore.directory().appendingPathComponent("keep.txt")
  try Data("preserve".utf8).write(to: other)
  defer { try? FileManager.default.removeItem(at: other) }
  GeneratorImageStore.remove(id: id)
  precondition(GeneratorImageStore.info(id: id) == nil)
  precondition(FileManager.default.fileExists(atPath: other.path))
  print("PASS: persistent image ID, orientation-safe bounded import, restore, cancel/empty input and scoped deletion")
 }
}
