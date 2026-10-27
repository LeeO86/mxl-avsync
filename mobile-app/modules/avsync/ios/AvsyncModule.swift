import ExpoModulesCore

public class AvsyncModule: Module {
  private weak var generator: AvsyncGeneratorController?
  public func definition() -> ModuleDefinition {
    Name("Avsync")
    Function("supportsLiveCapture") { true }
    Function("supportsGenerator") { true }
    AsyncFunction("generate") { (rate: Int, taiUtc: Double, promise: Promise) in
      guard (rate == 1 || rate == 2), taiUtc.isFinite, taiUtc >= 0, taiUtc <= 100,
            let parent = self.appContext?.utilities?.currentViewController(),
            parent.presentedViewController == nil else {
        promise.reject("GENERATOR_UNAVAILABLE", "Close the current screen and use 1 or 2 events/s."); return
      }
      let controller = AvsyncGeneratorController(rate: rate, taiUtc: taiUtc)
      self.generator = controller
      controller.onClose = { result in promise.resolve(result) }
      parent.present(controller, animated: true)
    }.runOnQueue(.main)
    View(AvsyncCameraView.self) {
      Events("onReport")
      Prop("active") { (view: AvsyncCameraView, active: Bool) in view.setActive(active) }
      Prop("taiUtcOffset") { (view: AvsyncCameraView, seconds: Double) in view.setTaiUtcOffset(seconds) }
      AsyncFunction("beginCalibration") { (view: AvsyncCameraView) in view.beginCalibration() }
      AsyncFunction("resumeAnalysis") { (view: AvsyncCameraView) in view.resumeAnalysis() }
    }
    OnDestroy {
      let controller = self.generator
      DispatchQueue.main.async { controller?.shutdown() }
    }
    Function("loadState") { () -> String? in
      UserDefaults.standard.string(forKey: "avsync.session.v1")
    }
    Function("saveState") { (json: String) in
      UserDefaults.standard.set(json, forKey: "avsync.session.v1")
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
}
