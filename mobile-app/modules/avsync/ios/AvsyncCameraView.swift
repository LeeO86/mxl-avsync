import AVFoundation
import Vision
import ExpoModulesCore

// Live camera + microphone capture for the shared PhoneAnalyzer (docs/mobile-native.md).
// Queues: control (session/device), samples (capture callbacks and the analyzer),
// qrWorker (Vision), analysis (polls a snapshot), main (preview and events).
final class AvsyncCameraView: ExpoView, AVCaptureVideoDataOutputSampleBufferDelegate, AVCaptureAudioDataOutputSampleBufferDelegate {
  let onReport = EventDispatcher()
  private let session = AVCaptureSession()
  private let control = DispatchQueue(label: "avsync.camera.control")
  private let samples = DispatchQueue(label: "avsync.camera.samples", qos: .userInitiated)
  private let analysis = DispatchQueue(label: "avsync.analysis", qos: .userInitiated, autoreleaseFrequency: .workItem)
  private let qrWorker = DispatchQueue(label: "avsync.qr", qos: .utility)
  private let processor = AVSyncPhoneAnalyzer()
  private let telemetry = CaptureTelemetry()
  private var preview: AVCaptureVideoPreviewLayer!
  private let pictureOutline = CAShapeLayer()
  private var outlineTracker = PreviewOutlineTracker()
  private var outlineWatchdog: Timer?
  private var notifications: [NSObjectProtocol] = []
  // control queue
  private var configured = false, cameraDevice: AVCaptureDevice?, requestedFps = 0.0, lockGeneration = 0
  // samples queue
  private var analysisBusy = false, qrBusy = false, lastQrScan = -10.0, lastPoll = 0.0, generation = 0
  private var focusing = false // tap to focus: frames are dropped until the new lock
  private var targetFps = 0.0, captureMode = "unknown", capturedSampleRate = 0.0, exposureMs = 0.0
  // main queue
  private var active = true, background = false

  required init(appContext: AppContext? = nil) {
    super.init(appContext: appContext)
    preview = AVCaptureVideoPreviewLayer(session: session)
    preview.videoGravity = .resizeAspectFill
    layer.addSublayer(preview)
    pictureOutline.fillColor = UIColor.clear.cgColor
    pictureOutline.lineWidth = 3
    pictureOutline.lineJoin = .round
    pictureOutline.actions = ["path": NSNull(), "strokeColor": NSNull()]
    layer.addSublayer(pictureOutline)
    outlineWatchdog = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in self?.drawPicture() }
    let center = NotificationCenter.default
    notifications.append(center.addObserver(forName: UIApplication.didEnterBackgroundNotification, object: nil, queue: .main) { [weak self] _ in self?.background = true; self?.updateRunning() })
    notifications.append(center.addObserver(forName: UIApplication.willEnterForegroundNotification, object: nil, queue: .main) { [weak self] _ in self?.background = false; self?.updateRunning() })
    notifications.append(center.addObserver(forName: AVAudioSession.routeChangeNotification, object: nil, queue: .main) { [weak self] _ in
      guard let self else { return }
      self.clearOutline()
      self.samples.async { self.restart(); self.emit(["kind": "error", "message": "Audio input or output changed · measurement restarted"]) }
    })
  }
  deinit {
    outlineWatchdog?.invalidate()
    for notification in notifications { NotificationCenter.default.removeObserver(notification) }
    let old = session
    control.async { if old.isRunning { old.stopRunning() } }
  }
  override func layoutSubviews() { super.layoutSubviews(); preview.frame = bounds; drawPicture() }
  override func didMoveToWindow() { super.didMoveToWindow(); updateRunning() }

  // MARK: props and methods

  func setActive(_ value: Bool) { active = value; updateRunning() }
  func setTaiUtcOffset(_ seconds: Double) { samples.async { [weak self] in self?.processor.setTaiUtcOffset(seconds) } }
  func setRequestedFps(_ fps: Double) {
    guard [0, 30, 60, 120, 240].contains(fps) else { return }
    control.async { [weak self] in
      guard let self, fps != self.requestedFps else { return }
      self.requestedFps = fps
      guard self.configured, let device = self.cameraDevice else { return }
      let running = self.session.isRunning
      if running { self.session.stopRunning() }
      do {
        self.session.beginConfiguration()
        defer { self.session.commitConfiguration() }
        try self.applyFrameRate(device)
      } catch {
        self.samples.async { self.emit(["kind": "error", "message": error.localizedDescription]) }
      }
      self.samples.sync { self.restart() }
      if running { self.session.startRunning(); self.scheduleExposureLock() }
    }
  }
  func beginCalibration() {
    DispatchQueue.main.async { [weak self] in self?.clearOutline() }
    samples.async { [weak self] in guard let self else { return }; self.generation += 1; self.focusing = false; self.processor.beginCalibration() }
  }
  // Tap to focus (x, y: 0…1 across the view): hold the analysis, re-run the auto step metered on
  // that spot, lock again, then start fresh, so no frame from the auto step reaches a reading.
  func focus(atX x: Double, y: Double) {
    DispatchQueue.main.async { [weak self] in
      guard let self, self.bounds.width > 0, self.bounds.height > 0 else { return }
      // The preview layer fills the view and knows its aspect fill and rotation.
      let point = self.preview.captureDevicePointConverted(fromLayerPoint: CGPoint(x: x * self.bounds.width, y: y * self.bounds.height))
      self.clearOutline()
      self.samples.async { self.generation += 1; self.focusing = true }
      self.control.async { self.scheduleExposureLock(at: point) }
    }
  }
  // Re-aim: drop the history and lock exposure again for the new view.
  func resumeAnalysis() {
    DispatchQueue.main.async { [weak self] in self?.clearOutline() }
    samples.async { [weak self] in self?.restart() }
    control.async { [weak self] in self?.scheduleExposureLock() }
  }

  // MARK: session

  private func restart() { generation += 1; focusing = false; processor.reset(); telemetry.reset(fps: targetFps) }
  private func updateRunning() {
    let shouldRun = window != nil && active && !background
    control.async { [weak self] in
      guard let self else { return }
      if shouldRun {
        do {
          if !self.configured { try self.configure() }
          if !self.session.isRunning {
            // The calibration review plays sound and may have switched the audio session to playback.
            try self.configureAudioSession()
            self.samples.sync { self.restart() }
            self.session.startRunning()
            self.scheduleExposureLock()
          }
        } catch { self.samples.async { self.emit(["kind": "error", "message": error.localizedDescription]) } }
      } else if self.session.isRunning {
        self.samples.sync { self.generation += 1 }
        self.session.stopRunning()
        DispatchQueue.main.async { self.clearOutline() }
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
    try configureAudioSession()
    session.beginConfiguration()
    defer { session.commitConfiguration() }
    session.automaticallyConfiguresApplicationAudioSession = false
    let videoInput = try AVCaptureDeviceInput(device: device)
    let audioInput = try AVCaptureDeviceInput(device: microphone)
    guard session.canAddInput(videoInput), session.canAddInput(audioInput) else {
      throw NSError(domain: "Avsync", code: 3, userInfo: [NSLocalizedDescriptionKey: "Cannot open the camera and microphone."])
    }
    session.addInput(videoInput); session.addInput(audioInput)
    let video = AVCaptureVideoDataOutput()
    video.videoSettings = [kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_420YpCbCr8BiPlanarFullRange]
    video.alwaysDiscardsLateVideoFrames = true
    video.setSampleBufferDelegate(self, queue: samples)
    let audio = AVCaptureAudioDataOutput()
    audio.setSampleBufferDelegate(self, queue: samples)
    guard session.canAddOutput(video), session.canAddOutput(audio) else {
      throw NSError(domain: "Avsync", code: 4, userInfo: [NSLocalizedDescriptionKey: "Cannot start synchronized capture."])
    }
    session.addOutput(video); session.addOutput(audio)
    if let connection = video.connection(with: .video) {
      if connection.isVideoOrientationSupported { connection.videoOrientation = .portrait }
      if connection.isVideoStabilizationSupported { connection.preferredVideoStabilizationMode = .off }
    }
    DispatchQueue.main.async { [weak self] in self?.preview.connection?.videoOrientation = .portrait }
    try applyFrameRate(device)
    cameraDevice = device
    configured = true
  }
  private func configureAudioSession() throws {
    let audioSession = AVAudioSession.sharedInstance()
    try audioSession.setCategory(.playAndRecord, mode: .measurement, options: [.defaultToSpeaker])
    try audioSession.setPreferredSampleRate(48000)
    try audioSession.setActive(true)
  }
  // 0 = automatic: the fastest mode the camera has (up to 240 fps).
  private func applyFrameRate(_ device: AVCaptureDevice) throws {
    var ranges = [CaptureFormatRange](), source = [(AVCaptureDevice.Format, AVFrameRateRange)]()
    for (index, format) in device.formats.enumerated() {
      let size = CMVideoFormatDescriptionGetDimensions(format.formatDescription)
      guard size.width >= 1280 else { continue }
      for range in format.videoSupportedFrameRateRanges where range.maxFrameRate <= 240.5 {
        ranges.append(CaptureFormatRange(format: index, width: Int(size.width), height: Int(size.height), minFps: range.minFrameRate, maxFps: range.maxFrameRate))
        source.append((format, range))
      }
    }
    guard let choice = chooseCaptureFormat(ranges, cap: requestedFps == 30 ? 60 : requestedFps) else {
      throw NSError(domain: "Avsync", code: 5, userInfo: [NSLocalizedDescriptionKey: "This camera rate is not supported."])
    }
    let selected = source[choice.range]
    let fps = requestedFps == 30 ? 30 : choice.fps
    try device.lockForConfiguration()
    defer { device.unlockForConfiguration() }
    device.activeFormat = selected.0
    let duration = abs(fps - selected.1.maxFrameRate) < 0.0001 ? selected.1.minFrameDuration : CMTime(seconds: 1 / fps, preferredTimescale: 1_000_000_000)
    device.activeVideoMinFrameDuration = duration
    device.activeVideoMaxFrameDuration = duration
    // Automatic again until the lock below.
    if device.isExposureModeSupported(.continuousAutoExposure) { device.exposureMode = .continuousAutoExposure }
    if device.isFocusModeSupported(.continuousAutoFocus) { device.focusMode = .continuousAutoFocus }
    if device.isWhiteBalanceModeSupported(.continuousAutoWhiteBalance) { device.whiteBalanceMode = .continuousAutoWhiteBalance }
    let size = CMVideoFormatDescriptionGetDimensions(selected.0.formatDescription)
    let mode = "\(size.width)x\(size.height)@\(Int(fps.rounded()))"
    samples.async { [weak self] in self?.targetFps = fps; self?.captureMode = mode }
  }
  // After 1.5 s of automatic exposure and focus: lock focus and white balance, and expose for
  // the whole frame interval at the ISO that keeps the same brightness. A full-frame exposure
  // turns each light edge into a ramp across one frame, which the core times exactly.
  // With a tap point, focus and exposure are metered there (device coordinates).
  private func scheduleExposureLock(at point: CGPoint? = nil) {
    lockGeneration += 1
    let ticket = lockGeneration
    if let device = cameraDevice, (try? device.lockForConfiguration()) != nil {
      if let point {
        if device.isFocusPointOfInterestSupported { device.focusPointOfInterest = point }
        if device.isExposurePointOfInterestSupported { device.exposurePointOfInterest = point }
      }
      if device.isExposureModeSupported(.continuousAutoExposure) { device.exposureMode = .continuousAutoExposure }
      if device.isFocusModeSupported(.continuousAutoFocus) { device.focusMode = .continuousAutoFocus }
      if device.isWhiteBalanceModeSupported(.continuousAutoWhiteBalance) { device.whiteBalanceMode = .continuousAutoWhiteBalance }
      device.unlockForConfiguration()
    }
    control.asyncAfter(deadline: .now() + 1.5) { [weak self] in
      guard let self, ticket == self.lockGeneration, let device = self.cameraDevice, self.session.isRunning,
            (try? device.lockForConfiguration()) != nil else { return }
      defer { device.unlockForConfiguration() }
      let frame = device.activeVideoMinFrameDuration, longest = device.activeFormat.maxExposureDuration
      let duration = CMTimeCompare(frame, longest) <= 0 ? frame : longest
      let iso = min(max(device.iso * Float(device.exposureDuration.seconds / duration.seconds), device.activeFormat.minISO), device.activeFormat.maxISO)
      if device.isExposureModeSupported(.custom) { device.setExposureModeCustom(duration: duration, iso: iso, completionHandler: nil) }
      if device.isFocusModeSupported(.locked) { device.focusMode = .locked }
      if device.isWhiteBalanceModeSupported(.locked) { device.whiteBalanceMode = .locked }
      let ms = duration.seconds * 1000
      self.samples.async { self.exposureMs = ms; if self.focusing { self.restart() } }
    }
  }

  // MARK: reports and preview

  private func captureContext() -> [String: Any] {
    var system = utsname(); uname(&system)
    let model = Mirror(reflecting: system.machine).children.compactMap { item -> String? in
      guard let value = item.value as? Int8, value != 0 else { return nil }; return String(UnicodeScalar(UInt8(bitPattern: value)))
    }.joined()
    let audio = AVAudioSession.sharedInstance()
    let route = audio.currentRoute.inputs.map { $0.portType.rawValue }.sorted().joined(separator: "+") + "/" + audio.currentRoute.outputs.map { $0.portType.rawValue }.sorted().joined(separator: "+")
    return ["platform": "ios", "model": model, "os": UIDevice.current.systemVersion, "audioRoute": route,
            "sampleRate": capturedSampleRate, "cameraMode": captureMode,
            "inputGain": Double(audio.inputGain), "inputGainSettable": audio.isInputGainSettable]
  }
  // samples queue
  private func emit(_ value: [String: Any]) {
    var report = value
    report["configuredFps"] = targetFps
    report["exposureMs"] = exposureMs
    report["captureContext"] = captureContext()
    report["captureStats"] = telemetry.snapshot()
    if let captureMs = value["timeMs"] as? Double, let offsetMs = value["deviceUtcOffsetMs"] as? Double, let clock = session.masterClock {
      let host = CMSyncConvertTime(CMTime(seconds: captureMs / 1000, preferredTimescale: 1_000_000_000), from: clock, to: CMClockGetHostTimeClock())
      let hostMs = CMTimeGetSeconds(host) * 1000
      if hostMs.isFinite {
        report["clockMapping"] = ["monotonicMs": hostMs, "deviceUnixMs": captureMs + offsetMs, "clockId": PhoneClock.identifier, "timebase": "ios-host"]
      }
    }
    DispatchQueue.main.async { [weak self] in self?.onReport(report) }
  }
  private func observePicture(_ value: [String: Any], at now: Double) {
    let rows = value["points"] as? [[String: Double]] ?? []
    let points = value["found"] as? Bool == true ? rows.compactMap { row -> CGPoint? in
      guard let x = row["x"], let y = row["y"] else { return nil }; return CGPoint(x: x, y: y)
    } : []
    outlineTracker.observe(points, image: CGSize(width: (value["width"] as? Double) ?? 0, height: (value["height"] as? Double) ?? 0), decoded: true, at: now)
    drawPicture()
  }
  private func clearOutline() { outlineTracker.reset(); pictureOutline.path = nil }
  private func drawPicture() {
    pictureOutline.frame = bounds
    guard let outline = outlineTracker.current(at: ProcessInfo.processInfo.systemUptime) else { pictureOutline.path = nil; return }
    let mapped = previewPoints(outline.points, image: outline.image, view: bounds.size)
    guard mapped.count == 4 else { pictureOutline.path = nil; return }
    let path = UIBezierPath(); path.move(to: mapped[0]); for point in mapped.dropFirst() { path.addLine(to: point) }; path.close()
    pictureOutline.path = path.cgPath
    pictureOutline.strokeColor = UIColor.systemGreen.cgColor
    pictureOutline.opacity = outline.held ? 0.45 : 1
  }

  // MARK: capture callbacks (samples queue)

  func captureOutput(_ output: AVCaptureOutput, didDrop sampleBuffer: CMSampleBuffer, from connection: AVCaptureConnection) {
    guard output is AVCaptureVideoDataOutput else { return }
    let value = CMGetAttachment(sampleBuffer, key: kCMSampleBufferAttachmentKey_DroppedFrameReason, attachmentModeOut: nil) as? String
    telemetry.drop(reason: value == kCMSampleBufferDroppedFrameReason_FrameWasLate as String ? "late" : value == kCMSampleBufferDroppedFrameReason_OutOfBuffers as String ? "outOfBuffers" : "other")
  }
  func captureOutput(_ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer, from connection: AVCaptureConnection) {
    guard !focusing else { return }
    let started = PhoneClock.nowMs(), isVideo = output is AVCaptureVideoDataOutput
    defer { telemetry.processing(isVideo ? "video" : "audio", ms: PhoneClock.nowMs() - started) }
    autoreleasepool {
      if isVideo {
        let frameTime = CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sampleBuffer)) * 1000
        telemetry.frame(at: frameTime)
        processor.pushVideo(sampleBuffer, exposureMs: exposureMs)
        scanQR(sampleBuffer, at: frameTime)
      } else {
        if let description = CMSampleBufferGetFormatDescription(sampleBuffer), let format = CMAudioFormatDescriptionGetStreamBasicDescription(description) { capturedSampleRate = format.pointee.mSampleRate }
        processor.pushAudio(sampleBuffer)
      }
      let now = ProcessInfo.processInfo.systemUptime
      if now - lastPoll > 0.1 { lastPoll = now; housekeeping(at: now) }
    }
  }
  private func scanQR(_ sampleBuffer: CMSampleBuffer, at frameTime: Double) {
    let now = ProcessInfo.processInfo.systemUptime
    guard !qrBusy, now - lastQrScan >= 0.1, processor.qrScanSuggested(at: frameTime), let pixel = CMSampleBufferGetImageBuffer(sampleBuffer) else { return }
    qrBusy = true; lastQrScan = now
    let ticket = generation
    qrWorker.async { [weak self] in
      let started = PhoneClock.nowMs()
      let detections = AVSyncQRCodeReader.read(pixel)?["detections"] as? [[String: Any]] ?? []
      let elapsed = PhoneClock.nowMs() - started
      self?.samples.async { [weak self] in
        guard let self else { return }
        self.qrBusy = false
        guard self.generation == ticket else { return }
        self.telemetry.processing("qrVision", ms: elapsed)
        for detection in detections { self.processor.observeQRDetection(detection, timeMs: frameTime) }
      }
    }
  }
  private func housekeeping(at now: Double) {
    if let clock = session.masterClock {
      let before = CMTimeGetSeconds(CMClockGetTime(clock)) * 1000
      let unix = Date().timeIntervalSince1970 * 1000
      let after = CMTimeGetSeconds(CMClockGetTime(clock)) * 1000
      processor.setDeviceUtcOffset(unix - (before + after) / 2)
    }
    let picture = processor.pictureObservation() as? [String: Any] ?? [:]
    DispatchQueue.main.async { [weak self] in self?.observePicture(picture, at: now) }
    if var report = processor.pollCalibration() as? [String: Any] {
      report["captureContext"] = captureContext()
      emit(report)
      return
    }
    guard !analysisBusy, processor.reportDue() else { return }
    // The audio scan is slow: poll a copy away from the capture queue, then merge its audio packets.
    let copyStarted = PhoneClock.nowMs()
    let snapshot = processor.snapshotForPoll()
    telemetry.processing("snapshot", ms: PhoneClock.nowMs() - copyStarted)
    analysisBusy = true
    let ticket = generation
    analysis.async { [weak self] in
      let started = PhoneClock.nowMs()
      let report = snapshot.poll() as? [String: Any]
      let elapsed = PhoneClock.nowMs() - started
      self?.samples.async { [weak self] in
        guard let self else { return }
        self.analysisBusy = false
        guard self.generation == ticket else { return }
        self.processor.mergeAudio(snapshot)
        self.telemetry.processing("poll", ms: elapsed)
        if let report { self.emit(report) }
      }
    }
  }
}
