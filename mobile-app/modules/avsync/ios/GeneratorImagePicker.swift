import PhotosUI
import UIKit

// Persist a small app-owned copy. A stable ID survives iOS container relocation;
// callers cannot supply an arbitrary file/network path to the renderer.
enum GeneratorImageStore {
  static func directory() throws -> URL {
    let root = try FileManager.default.url(for: .applicationSupportDirectory, in: .userDomainMask, appropriateFor: nil, create: true)
    let folder = root.appendingPathComponent("avsync-generator", isDirectory: true)
    try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
    return folder
  }
  static func url(id: String) throws -> URL {
    guard UUID(uuidString: id) != nil else { throw error("Invalid generator image ID.") }
    return try directory().appendingPathComponent(id + ".png")
  }
  static func error(_ message: String) -> NSError { NSError(domain: "Avsync", code: 30, userInfo: [NSLocalizedDescriptionKey: message]) }
  static func load(id: String) throws -> UIImage? {
    if id.isEmpty { return nil }
    guard let image = UIImage(contentsOfFile: try url(id: id).path) else { throw error("Saved image is unavailable. Choose it again.") }
    return image
  }
  static func info(id: String) -> [String: Any]? {
    guard let image = try? load(id: id), let file = try? url(id: id) else { return nil }
    return ["id": id, "uri": file.absoluteString, "width": image.size.width, "height": image.size.height]
  }
  static func save(_ image: UIImage) throws -> [String: Any] {
    guard image.size.width > 0, image.size.height > 0 else { throw error("The selected image is empty.") }
    let scale = min(1, 512 / max(image.size.width, image.size.height))
    let size = CGSize(width: max(1, (image.size.width*scale).rounded()), height: max(1, (image.size.height*scale).rounded()))
    let format = UIGraphicsImageRendererFormat.default(); format.scale = 1
    let rendered = UIGraphicsImageRenderer(size: size, format: format).image { _ in image.draw(in: CGRect(origin: .zero, size: size)) }
    guard let data = rendered.pngData() else { throw error("The selected image could not be imported.") }
    let id = UUID().uuidString, file = try url(id: id)
    try data.write(to: file, options: .atomic)
    return ["id": id, "uri": file.absoluteString, "width": size.width, "height": size.height]
  }
  static func remove(id: String) { if let file = try? url(id: id) { try? FileManager.default.removeItem(at: file) } }
}

final class GeneratorImagePicker: NSObject, PHPickerViewControllerDelegate {
  var onFinish: ((Result<[String: Any]?, Error>) -> Void)?
  private var loading: Progress?
  private weak var presented: PHPickerViewController?
  func present(from parent: UIViewController) {
    var configuration = PHPickerConfiguration()
    configuration.filter = .images; configuration.selectionLimit = 1
    let picker = PHPickerViewController(configuration: configuration)
    picker.delegate = self; presented = picker
    parent.present(picker, animated: true)
  }
  func picker(_ picker: PHPickerViewController, didFinishPicking results: [PHPickerResult]) {
    picker.dismiss(animated: true); presented = nil
    guard let provider = results.first?.itemProvider else { complete(.success(nil)); return }
    guard provider.canLoadObject(ofClass: UIImage.self) else { complete(.failure(GeneratorImageStore.error("Choose a supported image."))); return }
    loading = provider.loadObject(ofClass: UIImage.self) { [weak self] object, failure in
      guard let image = object as? UIImage else { self?.complete(.failure(failure ?? GeneratorImageStore.error("Image import failed."))); return }
      do { self?.complete(.success(try GeneratorImageStore.save(image))) }
      catch { self?.complete(.failure(error)) }
    }
  }
  private func complete(_ result: Result<[String: Any]?, Error>) {
    DispatchQueue.main.async { [self] in loading = nil; let callback = onFinish; onFinish = nil; callback?(result) }
  }
  func cancel() { loading?.cancel(); loading = nil; presented?.dismiss(animated: false); presented = nil; complete(.success(nil)) }
}
