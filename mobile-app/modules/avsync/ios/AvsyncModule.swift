import ExpoModulesCore

public class AvsyncModule: Module {
  private var imagePicker: GeneratorImagePicker?
  private weak var generator: AvsyncGeneratorController?
  public func definition() -> ModuleDefinition {
    Name("Avsync")
    Function("supportsLiveCapture") { true }
    Function("supportsGenerator") { true }
    AsyncFunction("generate") { (rate: Int, taiUtc: Double, promise: Promise) in
      self.presentGenerator(rate: rate, taiUtc: taiUtc, options: [:], promise: promise)
    }.runOnQueue(.main)
    AsyncFunction("generateStyled") { (rate: Int, taiUtc: Double, options: [String: String], promise: Promise) in
      self.presentGenerator(rate: rate, taiUtc: taiUtc, options: options, promise: promise)
    }.runOnQueue(.main)
    Function("generatorImageInfo") { (id: String) -> [String: Any]? in GeneratorImageStore.info(id: id) }
    AsyncFunction("pickGeneratorImage") { (promise: Promise) in
      guard self.imagePicker == nil, let parent = self.appContext?.utilities?.currentViewController(), parent.presentedViewController == nil else {
        promise.reject("IMAGE_PICKER_BUSY", "Close the current screen first."); return
      }
      let picker = GeneratorImagePicker(); self.imagePicker = picker
      picker.onFinish = { [weak self] result in
        self?.imagePicker = nil
        switch result { case .success(let image): promise.resolve(image)
        case .failure(let error): promise.reject("IMAGE_IMPORT_FAILED", error.localizedDescription) }
      }
      parent.view.endEditing(true)
      picker.present(from: parent)
    }.runOnQueue(.main)
    View(AvsyncCameraView.self) {
      Events("onReport")
      Prop("active") { (view: AvsyncCameraView, active: Bool) in view.setActive(active) }
      Prop("taiUtcOffset") { (view: AvsyncCameraView, seconds: Double) in view.setTaiUtcOffset(seconds) }
      AsyncFunction("beginCalibration") { (view: AvsyncCameraView) in view.beginCalibration() }
      AsyncFunction("resumeAnalysis") { (view: AvsyncCameraView) in view.resumeAnalysis() }
    }
    OnDestroy {
      let controller = self.generator, picker = self.imagePicker
      DispatchQueue.main.async { controller?.shutdown(); picker?.cancel() }
    }
    Function("loadState") { () -> String? in
      UserDefaults.standard.string(forKey: "avsync.session.v1")
    }
    Function("saveState") { (json: String) in
      let old = self.imageID(in: UserDefaults.standard.string(forKey: "avsync.session.v1"))
      let next = self.imageID(in: json)
      UserDefaults.standard.set(json, forKey: "avsync.session.v1")
      if let old, old != next { GeneratorImageStore.remove(id: old) }
    }
    Function("deviceVersion") { () -> String in
      UIDevice.current.systemVersion
    }
    AsyncFunction("analyze") { (uri: String, calibration: Bool, promise: Promise) in
      DispatchQueue.global(qos: .userInitiated).async {
        let result = AVSyncFileAnalyzer.analyze(uri, calibration: calibration)
        if let error = result["error"] as? String {
          promise.reject("ANALYSIS_FAILED", error)
        } else {
          promise.resolve(result)
        }
      }
    }
  }
  private func imageID(in json: String?) -> String? {
    guard let data = json?.data(using: .utf8), let value = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any] else { return nil }
    return value["generatorImageId"] as? String
  }
  private func presentGenerator(rate: Int, taiUtc: Double, options: [String: String], promise: Promise) {
    guard (rate == 1 || rate == 2), taiUtc.isFinite, taiUtc >= 0, taiUtc <= 100,
          let parent = appContext?.utilities?.currentViewController(), parent.presentedViewController == nil else {
      promise.reject("GENERATOR_UNAVAILABLE", "Close the current screen and use 1 or 2 events/s."); return
    }
    func label(_ key: String, limit: Int) -> String {
      String((options[key] ?? "").components(separatedBy: .whitespacesAndNewlines).filter { !$0.isEmpty }.joined(separator: " ").prefix(limit))
    }
    do {
      let image = try GeneratorImageStore.load(id: options["imageId"] ?? "")
      let controller = AvsyncGeneratorController(rate: rate, taiUtc: taiUtc, name: label("name", limit: 64), subtitle: label("subtitle", limit: 128), image: image)
      generator = controller; controller.onClose = { result in promise.resolve(result) }
      parent.view.endEditing(true)
      parent.present(controller, animated: true)
    } catch { promise.reject("GENERATOR_IMAGE_UNAVAILABLE", error.localizedDescription) }
  }

}
