import AVFoundation
import Vision
import ExpoModulesCore

final class AvsyncCameraView: ExpoView, AVCaptureVideoDataOutputSampleBufferDelegate, AVCaptureAudioDataOutputSampleBufferDelegate {
  let onReport = EventDispatcher()
  private let session = AVCaptureSession()
  private let control = DispatchQueue(label: "avsync.camera.control")
  private let samples = DispatchQueue(label: "avsync.camera.samples", qos: .userInitiated)
  private let processor = AVSyncStreamAnalyzer()
  private var preview: AVCaptureVideoPreviewLayer!
  private var configured = false
  private var targetFps = 0.0
  private var lastPoll = 0.0
  private var taiUtcSeconds = 37.0
  private let qrWorker = DispatchQueue(label: "avsync.qr", qos: .utility)
  private var qrBusy = false
  private var lastQrScan = -10.0
  private var generation = 0
  private let pictureOutline = CAShapeLayer()
  private var lastPicture: [String: Any] = [:]
  private var lastPictureAt = -10.0
  private var outlineWatchdog: Timer?
  private var active = true
  private var background = false
  private let diagnostics = DispatchQueue(label: "avsync.diagnostics")
  private var diagnosticReports: [[String: Any]] = []
  private var notifications: [NSObjectProtocol] = []

  required init(appContext: AppContext? = nil) {
    super.init(appContext: appContext)
    if let directory = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask).first,
       let data = try? Data(contentsOf: directory.appendingPathComponent("avsync-diagnostics.json")),
       let rows = (try? JSONSerialization.jsonObject(with: data)) as? [[String: Any]] {
      diagnosticReports = Array(rows.suffix(120))
    }
    preview = AVCaptureVideoPreviewLayer(session: session)
    preview.videoGravity = .resizeAspectFill
    layer.addSublayer(preview)
    pictureOutline.fillColor = UIColor.clear.cgColor
    pictureOutline.lineWidth = 3
    pictureOutline.lineJoin = .round
    pictureOutline.actions = ["path": NSNull(), "strokeColor": NSNull()]
    layer.addSublayer(pictureOutline)
    outlineWatchdog = Timer.scheduledTimer(withTimeInterval: 0.2, repeats: true) { [weak self] _ in
      guard let self, ProcessInfo.processInfo.systemUptime - self.lastPictureAt > 0.35 else { return }
      self.lastPicture = [:]; self.drawPicture()
    }
    notifications.append(NotificationCenter.default.addObserver(forName: UIApplication.didEnterBackgroundNotification, object: nil, queue: .main) { [weak self] _ in self?.background = true; self?.updateRunning() })
    notifications.append(NotificationCenter.default.addObserver(forName: UIApplication.willEnterForegroundNotification, object: nil, queue: .main) { [weak self] _ in self?.background = false; self?.updateRunning() })
  }
  override func layoutSubviews() { super.layoutSubviews(); preview.frame = bounds; drawPicture() }
  private func drawPicture() {
    pictureOutline.frame = bounds
    guard lastPicture["found"] as? Bool == true,
          let points = lastPicture["points"] as? [[String: Double]], points.count == 4,
          let width = lastPicture["width"] as? Double, let height = lastPicture["height"] as? Double else { pictureOutline.path = nil; return }
    let mapped = previewPoints(points.map { CGPoint(x: $0["x"] ?? 0, y: $0["y"] ?? 0) }, image: CGSize(width: width, height: height), view: bounds.size)
    guard mapped.count == 4 else { pictureOutline.path = nil; return }
    let path = UIBezierPath(); path.move(to: mapped[0]); for point in mapped.dropFirst() { path.addLine(to: point) }; path.close()
    pictureOutline.path = path.cgPath
    pictureOutline.strokeColor = (lastPicture["codeDecoded"] as? Bool == true ? UIColor.systemGreen : UIColor.systemOrange).cgColor
  }
  func setTaiUtcOffset(_ seconds: Double) { samples.async { [weak self] in self?.taiUtcSeconds = seconds } }
  override func didMoveToWindow() { super.didMoveToWindow(); updateRunning() }
  deinit {
    outlineWatchdog?.invalidate()
    for notification in notifications { NotificationCenter.default.removeObserver(notification) }
    let old = session
    control.async { if old.isRunning { old.stopRunning() } }
  }
  func setActive(_ value: Bool) { active = value; updateRunning() }
  func beginCalibration() { samples.async { [weak self] in self?.generation += 1; self?.processor.beginCalibration() } }
  func resumeAnalysis() { samples.async { [weak self] in self?.generation += 1; self?.processor.reset() } }
  private func emit(_ value: [String: Any]) {
    var report = value; report["configuredFps"] = targetFps
    if report["kind"] as? String != "calibration" {
      let snapshot = report
      diagnostics.async { [weak self] in
        guard let self else { return }
        var row = snapshot; row["savedAt"] = Date().timeIntervalSince1970 * 1000
        self.diagnosticReports.append(row)
        if self.diagnosticReports.count > 120 { self.diagnosticReports.removeFirst() }
        if let data = try? JSONSerialization.data(withJSONObject: self.diagnosticReports),
           let directory = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask).first {
          try? data.write(to: directory.appendingPathComponent("avsync-diagnostics.json"), options: .atomic)
        }
      }
    }
    DispatchQueue.main.async { [weak self] in self?.onReport(report) }
  }
  private func updateRunning() {
    let shouldRun = window != nil && active && !background
    control.async { [weak self] in
      guard let self else { return }
      if shouldRun {
        do {
          if !self.configured { try self.configure() }
          if !self.session.isRunning { self.samples.async { self.generation += 1; self.processor.reset() }; self.session.startRunning() }
        } catch { self.emit(["kind": "error", "message": error.localizedDescription]) }
      } else if self.session.isRunning {
        self.session.stopRunning()
        self.samples.async { self.generation += 1 }
        DispatchQueue.main.async { self.lastPicture = [:]; self.drawPicture() }
      }
    }
  }
  private func configure() throws {
    guard AVCaptureDevice.authorizationStatus(for: .video) == .authorized,
          AVCaptureDevice.authorizationStatus(for: .audio) == .authorized else {
      throw NSError(domain: "Avsync", code: 1, userInfo: [NSLocalizedDescriptionKey: "Allow camera and microphone access."])
    }
    guard let device = AVCaptureDevice.default(.builtInWideAngleCamera, for: .video, position: .back),
          let microphone = AVCaptureDevice.default(for: .audio) else {
      throw NSError(domain: "Avsync", code: 2, userInfo: [NSLocalizedDescriptionKey: "Camera or microphone unavailable on this device."])
    }
    let audioSession = AVAudioSession.sharedInstance()
    try audioSession.setCategory(.playAndRecord, mode: .measurement, options: [.defaultToSpeaker])
    try audioSession.setPreferredSampleRate(48000)
    try audioSession.setActive(true)
    session.beginConfiguration()
    defer { session.commitConfiguration() }
    session.automaticallyConfiguresApplicationAudioSession = false
    let videoInput = try AVCaptureDeviceInput(device: device)
    let audioInput = try AVCaptureDeviceInput(device: microphone)
    guard session.canAddInput(videoInput), session.canAddInput(audioInput) else { throw NSError(domain: "Avsync", code: 3, userInfo: [NSLocalizedDescriptionKey: "Cannot configure camera/audio input."]) }
    session.addInput(videoInput); session.addInput(audioInput)
    let video = AVCaptureVideoDataOutput()
    video.videoSettings = [kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_420YpCbCr8BiPlanarFullRange]
    video.alwaysDiscardsLateVideoFrames = true
    video.setSampleBufferDelegate(self, queue: samples)
    let audio = AVCaptureAudioDataOutput()
    audio.setSampleBufferDelegate(self, queue: samples)
    guard session.canAddOutput(video), session.canAddOutput(audio) else { throw NSError(domain: "Avsync", code: 4, userInfo: [NSLocalizedDescriptionKey: "Cannot configure synchronized capture outputs."]) }
    session.addOutput(video); session.addOutput(audio)
    if let connection = video.connection(with: .video) {
      if connection.isVideoOrientationSupported { connection.videoOrientation = .portrait }
      if connection.isVideoStabilizationSupported { connection.preferredVideoStabilizationMode = .off }
    }
    DispatchQueue.main.async { [weak self] in self?.preview.connection?.videoOrientation = .portrait }
    let choices = device.formats.flatMap { format in format.videoSupportedFrameRateRanges.map { (format, $0) } }
    guard let best = choices.max(by: { a, b in
      if a.1.maxFrameRate != b.1.maxFrameRate { return a.1.maxFrameRate < b.1.maxFrameRate }
      let x = CMVideoFormatDescriptionGetDimensions(a.0.formatDescription), y = CMVideoFormatDescriptionGetDimensions(b.0.formatDescription)
      return Int64(x.width) * Int64(x.height) < Int64(y.width) * Int64(y.height)
    }) else { throw NSError(domain: "Avsync", code: 5, userInfo: [NSLocalizedDescriptionKey: "No supported camera format."]) }
    try device.lockForConfiguration()
    device.activeFormat = best.0
    device.activeVideoMinFrameDuration = best.1.minFrameDuration
    device.activeVideoMaxFrameDuration = best.1.minFrameDuration
    device.unlockForConfiguration()
    targetFps = best.1.maxFrameRate
    configured = true
    emit(["kind": "status", "fps": 0, "message": "Camera ready. Live analysis starts as frames and audio arrive."])
  }
  func captureOutput(_ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer, from connection: AVCaptureConnection) {
    autoreleasepool {
      if output is AVCaptureVideoDataOutput {
        processor.pushVideo(sampleBuffer)
        let scanTime = ProcessInfo.processInfo.systemUptime
        if !qrBusy && scanTime - lastQrScan >= 1, let pixel = CMSampleBufferGetImageBuffer(sampleBuffer) {
          qrBusy = true; lastQrScan = scanTime
          let timestamp = CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sampleBuffer)) * 1000
          let captureGeneration = generation
          qrWorker.async { [weak self] in
            let request = VNDetectBarcodesRequest(); request.symbologies = [.qr]
            try? VNImageRequestHandler(cvPixelBuffer: pixel, orientation: .up).perform([request])
            let texts = request.results?.compactMap { $0.payloadStringValue } ?? []
            self?.samples.async { [weak self] in
              guard let self else { return }; self.qrBusy = false
              guard self.generation == captureGeneration else { return }
              for value in texts where value.contains("mxl-avsync/1") { self.processor.observeQR(value, timeMs: timestamp) }
            }
          }
        }
      } else { processor.pushAudio(sampleBuffer) }
      let now = ProcessInfo.processInfo.systemUptime
      if now - lastPoll > 0.1 {
        lastPoll = now
        if let clock = session.masterClock {
          let before = CMTimeGetSeconds(CMClockGetTime(clock)) * 1000
          let unix = Date().timeIntervalSince1970 * 1000
          let after = CMTimeGetSeconds(CMClockGetTime(clock)) * 1000
          processor.setClockAnchor((before + after) / 2, unixTimeMs: unix)
        }
        processor.setTaiUtcOffset(taiUtcSeconds)
        let picture = processor.pictureObservation() as? [String: Any] ?? [:]
        DispatchQueue.main.async { [weak self] in self?.lastPicture = picture; self?.lastPictureAt = ProcessInfo.processInfo.systemUptime; self?.drawPicture() }
        if let report = processor.poll() as? [String: Any] { emit(report) }
      }
    }
  }
}
