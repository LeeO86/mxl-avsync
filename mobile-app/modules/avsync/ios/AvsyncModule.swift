import ExpoModulesCore

public class AvsyncModule: Module {
  public func definition() -> ModuleDefinition {
    Name("Avsync")
    Function("supportsLiveCapture") { true }
    View(AvsyncCameraView.self) {
      Events("onReport")
      Prop("active") { (view: AvsyncCameraView, active: Bool) in view.setActive(active) }
      Prop("taiUtcOffset") { (view: AvsyncCameraView, seconds: Double) in view.setTaiUtcOffset(seconds) }
      AsyncFunction("beginCalibration") { (view: AvsyncCameraView) in view.beginCalibration() }
      AsyncFunction("resumeAnalysis") { (view: AvsyncCameraView) in view.resumeAnalysis() }
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
