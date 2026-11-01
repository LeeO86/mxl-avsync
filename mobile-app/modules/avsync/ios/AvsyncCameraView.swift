import AVFoundation
import Vision
import ExpoModulesCore

final class AvsyncCameraView: ExpoView, AVCaptureVideoDataOutputSampleBufferDelegate, AVCaptureAudioDataOutputSampleBufferDelegate {
  let onReport = EventDispatcher()
  private let session = AVCaptureSession()
  private let control = DispatchQueue(label: "avsync.camera.control")
  private let samples = DispatchQueue(label: "avsync.camera.samples", qos: .userInitiated)
  private let processor = AVSyncStreamAnalyzer()
  private let analysis = DispatchQueue(label:"avsync.analysis",qos:.userInitiated,autoreleaseFrequency:.workItem)
  private var analysisBusy = false
  private var preview: AVCaptureVideoPreviewLayer!
  private var configured = false
  private var targetFps = 0.0
  private var requestedFps = 120.0
  private var cameraDevice: AVCaptureDevice?
  private let telemetry = CaptureTelemetry()
  private var lastPoll = 0.0
  private var taiUtcSeconds = 37.0
  private let qrWorker = DispatchQueue(label: "avsync.qr", qos: .utility)
  private var qrBusy = false
  private var lastQrScan = -10.0
  private var generation = 0
  private var captureMode = "unknown"
  private var capturedSampleRate = 0.0
  private let pictureOutline = CAShapeLayer()
  private var outlineTracker = PreviewOutlineTracker()
  private var outlineWatchdog: Timer?
  private var active = true
  private var background = false
  private let diagnostics = DispatchQueue(label: "avsync.diagnostics")
  private var diagnosticReports: [[String: Any]] = []
  private var notifications: [NSObjectProtocol] = []
  private var routeSignature: String?
  private var contextCache: [String:Any]?
  private static let deviceIdentity: [String:String] = {
    var system=utsname();uname(&system)
    let model=Mirror(reflecting:system.machine).children.compactMap { item -> String? in
      guard let value=item.value as? Int8,value != 0 else{return nil};return String(UnicodeScalar(UInt8(bitPattern:value)))
    }.joined()
    return ["platform":"ios","model":model,"os":UIDevice.current.systemVersion]
  }()

  required init(appContext: AppContext? = nil) {
    super.init(appContext: appContext)
    processor.setSparseAcquisition(true)
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
    outlineWatchdog = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in self?.drawPicture() }
    notifications.append(NotificationCenter.default.addObserver(forName: UIApplication.didEnterBackgroundNotification, object: nil, queue: .main) { [weak self] _ in self?.background = true; self?.updateRunning() })
    notifications.append(NotificationCenter.default.addObserver(forName: UIApplication.willEnterForegroundNotification, object: nil, queue: .main) { [weak self] _ in self?.background = false; self?.updateRunning() })
    notifications.append(NotificationCenter.default.addObserver(forName: AVAudioSession.routeChangeNotification, object:nil, queue:nil) { [weak self] _ in
      guard let self else { return }
      DispatchQueue.main.async { [weak self] in self?.clearOutline() }
      self.samples.async { self.generation += 1;self.routeSignature=nil;self.contextCache=nil;self.processor.reset(); self.emit(["kind":"error","message":"Audio route changed · capture restarted; check calibration compatibility"]) }
    })
  }
  private func captureContext() -> [String:Any] {
    if let contextCache,contextCache["sampleRate"] as? Double==capturedSampleRate,contextCache["cameraMode"] as? String==captureMode,routeSignature != nil {return contextCache}
    if routeSignature==nil {
      let route=AVAudioSession.sharedInstance().currentRoute
      routeSignature=route.inputs.map{$0.portType.rawValue}.sorted().joined(separator:"+")+"/"+route.outputs.map{$0.portType.rawValue}.sorted().joined(separator:"+")
    }
    var result:[String:Any]=Self.deviceIdentity
    result["audioRoute"]=routeSignature;result["sampleRate"]=capturedSampleRate;result["cameraMode"]=captureMode
    contextCache=result;return result
  }
  override func layoutSubviews() { super.layoutSubviews(); preview.frame = bounds; drawPicture() }
  private func observePicture(_ value: [String: Any], at now: Double) {
    let rows = value["points"] as? [[String: Double]] ?? []
    let points = value["found"] as? Bool == true ? rows.compactMap { row -> CGPoint? in
      guard let x=row["x"], let y=row["y"] else { return nil }; return CGPoint(x:x,y:y)
    } : []
    outlineTracker.observe(points, image:CGSize(width:(value["width"] as? Double) ?? 0,height:(value["height"] as? Double) ?? 0), decoded:value["codeDecoded"] as? Bool == true, at:now)
    drawPicture()
  }
  private func clearOutline() { outlineTracker.reset(); pictureOutline.path=nil }
  private func drawPicture() {
    pictureOutline.frame = bounds
    guard let outline=outlineTracker.current(at:ProcessInfo.processInfo.systemUptime) else { pictureOutline.path=nil; return }
    let mapped=previewPoints(outline.points,image:outline.image,view:bounds.size)
    guard mapped.count == 4 else { pictureOutline.path=nil;return }
    let path=UIBezierPath();path.move(to:mapped[0]);for point in mapped.dropFirst(){path.addLine(to:point)};path.close()
    pictureOutline.path=path.cgPath
    pictureOutline.strokeColor=(outline.decoded ? UIColor.systemGreen : UIColor.systemOrange).cgColor
    pictureOutline.opacity=outline.held ? 0.45 : 1
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
  func setRequestedFps(_ fps: Double) {
    guard [0,60,120,240].contains(fps) else { return }
    control.async { [weak self] in
      guard let self, fps != self.requestedFps else { return }
      let old=self.requestedFps
      guard self.configured, let device=self.cameraDevice else { self.requestedFps=fps;return }
      let wasRunning=self.session.isRunning
      if wasRunning { self.session.stopRunning() }
      self.samples.sync { self.generation += 1 } // Invalidate old DSP before changing mode/report identity.
      self.requestedFps=fps
      do {
        self.session.beginConfiguration()
        do { try self.applyFrameRate(device) } catch { self.session.commitConfiguration();throw error }
        self.session.commitConfiguration()
        self.samples.sync { self.generation += 1;self.processor.reset();self.telemetry.reset(fps:self.targetFps) }
        DispatchQueue.main.async { self.clearOutline() }
        self.samples.async { self.emit(["kind":"status","fps":0,"message":"Capture rate changed · collecting fresh events"]) }
      } catch {
        self.requestedFps=old
        self.samples.async { self.emit(["kind":"error","message":error.localizedDescription,"requestedFps":fps,"captureRateRejected":old]) }
      }
      if wasRunning { self.session.startRunning() }
    }
  }

  func beginCalibration() { DispatchQueue.main.async { [weak self] in self?.clearOutline() }; samples.async { [weak self] in self?.generation += 1; self?.processor.beginCalibration() } }
  func resumeAnalysis() { DispatchQueue.main.async { [weak self] in self?.clearOutline() }; samples.async { [weak self] in self?.generation += 1; self?.processor.reset() } }
  private func emit(_ value: [String: Any]) {
    var report = value; report["configuredFps"] = targetFps; report["captureContext"] = captureContext();report["requestedFps"] = value["requestedFps"] ?? requestedFps
    report["captureStats"] = telemetry.snapshot(events:value["events"] as? [[String:Any]] ?? [])
    if let captureMs = value["timeMs"] as? Double, let clock = session.masterClock,
       let offsetMs = value["deviceUtcOffsetMs"] as? Double {
      let host = CMSyncConvertTime(CMTime(seconds:captureMs/1000,preferredTimescale:1_000_000_000), from:clock, to:CMClockGetHostTimeClock())
      let hostMs = CMTimeGetSeconds(host)*1000
      if hostMs.isFinite {
        report["clockMapping"] = ["monotonicMs":hostMs,"deviceUnixMs":captureMs+offsetMs,
                                 "clockId":PhoneClock.identifier,"timebase":"ios-host"]
      }
    }
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
          if !self.session.isRunning { self.samples.sync { self.generation += 1; self.processor.reset();self.telemetry.reset(fps:self.targetFps) }; self.session.startRunning() }
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
    try applyFrameRate(device)
    cameraDevice=device
    samples.sync { telemetry.reset(fps:targetFps) }
    configured = true
    samples.async { self.emit(["kind": "status", "fps": 0, "message": "Camera ready. Live analysis starts as frames and audio arrive."]) }
  }
  private func applyFrameRate(_ device: AVCaptureDevice) throws {
    let formats=device.formats
    var ranges=[CaptureFormatRange](), source=[(AVCaptureDevice.Format, AVFrameRateRange)]()
    for (index,format) in formats.enumerated() {
      let size=CMVideoFormatDescriptionGetDimensions(format.formatDescription)
      for range in format.videoSupportedFrameRateRanges {
        ranges.append(CaptureFormatRange(format:index,width:Int(size.width),height:Int(size.height),minFps:range.minFrameRate,maxFps:range.maxFrameRate))
        source.append((format,range))
      }
    }
    guard let choice=chooseCaptureFormat(ranges,cap:requestedFps) else {
      throw NSError(domain:"Avsync",code:5,userInfo:[NSLocalizedDescriptionKey:"Requested camera rate is unsupported. Choose another rate."])
    }
    let selected=source[choice.range]
    try device.lockForConfiguration()
    defer { device.unlockForConfiguration() }
    device.activeFormat=selected.0
    let duration=abs(choice.fps-selected.1.maxFrameRate)<0.0001 ? selected.1.minFrameDuration : CMTime(seconds:1/choice.fps,preferredTimescale:1_000_000_000)
    device.activeVideoMinFrameDuration=duration;device.activeVideoMaxFrameDuration=duration
    targetFps=choice.fps
    let size=CMVideoFormatDescriptionGetDimensions(selected.0.formatDescription)
    captureMode="\(size.width)x\(size.height)@\(choice.fps)"
  }
  func captureOutput(_ output: AVCaptureOutput, didDrop sampleBuffer: CMSampleBuffer, from connection: AVCaptureConnection) {
    guard output is AVCaptureVideoDataOutput else { return }
    let value=CMGetAttachment(sampleBuffer,key:kCMSampleBufferAttachmentKey_DroppedFrameReason,attachmentModeOut:nil) as? String
    let reason=value == kCMSampleBufferDroppedFrameReason_FrameWasLate as String ? "late" : value == kCMSampleBufferDroppedFrameReason_OutOfBuffers as String ? "outOfBuffers" : value == kCMSampleBufferDroppedFrameReason_Discontinuity as String ? "discontinuity" : "unknown"
    telemetry.drop(reason:reason)
  }
  func captureOutput(_ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer, from connection: AVCaptureConnection) {
    let started=PhoneClock.nowMs(),isVideo=output is AVCaptureVideoDataOutput
    defer { telemetry.processing(isVideo ? "video" : "audio",ms:PhoneClock.nowMs()-started) }
    autoreleasepool {
      if isVideo {
        telemetry.frame(at:CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sampleBuffer))*1000)
        let work=processor.pushVideo(sampleBuffer)
        telemetry.processing("videoLock",ms:work.lockMs)
        telemetry.processing("videoProbe",ms:work.processMs)
        telemetry.processing("videoUnlock",ms:work.unlockMs)
        telemetry.videoPath(Int(work.path))
        let scanTime = ProcessInfo.processInfo.systemUptime
        let frameTime=CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sampleBuffer))*1000
        if !qrBusy && scanTime - lastQrScan >= 0.1, processor.qrScanSuggested(at:frameTime), let pixel = CMSampleBufferGetImageBuffer(sampleBuffer) {
          qrBusy = true; lastQrScan = scanTime
          let timestamp = CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sampleBuffer)) * 1000
          let captureGeneration = generation
          qrWorker.async { [weak self] in
            let qrStarted=PhoneClock.nowMs()
            let result=AVSyncQRCodeReader.read(pixel) ?? [:]
            let detections = result["detections"] as? [[String: Any]] ?? []
            let timings=result["timingsMs"] as? [NSNumber] ?? []
            let qrElapsed=PhoneClock.nowMs()-qrStarted
            self?.samples.async { [weak self] in
              guard let self else { return }; self.qrBusy = false
              guard self.generation == captureGeneration else { return }
              self.telemetry.processing("qrVision",ms:qrElapsed)
              if let first=timings.first { self.telemetry.processing("qrVisionFirst",ms:first.doubleValue) }
              if timings.count>1 { self.telemetry.processing("qrVisionFallback",ms:timings[1].doubleValue) }
              let applyStarted=PhoneClock.nowMs()
              for detection in detections { self.processor.observeQRDetection(detection, timeMs: timestamp) }
              self.telemetry.processing("qrApply",ms:PhoneClock.nowMs()-applyStarted)
            }
          }
        }
      } else {
        if let description = CMSampleBufferGetFormatDescription(sampleBuffer), let format = CMAudioFormatDescriptionGetStreamBasicDescription(description) { capturedSampleRate = format.pointee.mSampleRate }
        processor.pushAudio(sampleBuffer)
      }
      let now = ProcessInfo.processInfo.systemUptime
      if now - lastPoll > 0.1 {
        let housekeepingStarted=PhoneClock.nowMs()
        defer { telemetry.processing("housekeeping",ms:PhoneClock.nowMs()-housekeepingStarted) }
        lastPoll = now
        if let clock = session.masterClock {
          let before = CMTimeGetSeconds(CMClockGetTime(clock)) * 1000
          let unix = Date().timeIntervalSince1970 * 1000
          let after = CMTimeGetSeconds(CMClockGetTime(clock)) * 1000
          processor.setClockAnchor((before + after) / 2, unixTimeMs: unix)
        }
        processor.setTaiUtcOffset(taiUtcSeconds)
        let picture = processor.pictureObservation() as? [String: Any] ?? [:]
        DispatchQueue.main.async { [weak self] in self?.observePicture(picture, at:now) }
        if let report=processor.pollCalibration() as? [String:Any] { emit(report) }
        else if !analysisBusy {
          let copyStart=PhoneClock.nowMs()
          if let snapshot=processor.liveSnapshotForPolling() {
            telemetry.processing("snapshot",ms:PhoneClock.nowMs()-copyStart)
            analysisBusy=true
            let captureGeneration=generation
            let captureSignature=captureContext()
            analysis.async { [weak self] in
              let started=PhoneClock.nowMs()
              let report=snapshot.poll() as? [String:Any]
              let elapsed=PhoneClock.nowMs()-started
              self?.samples.async { [weak self] in
                guard let self else { return };self.analysisBusy=false
                let deliveryStarted=PhoneClock.nowMs()
                defer { self.telemetry.processing("reportDelivery",ms:PhoneClock.nowMs()-deliveryStarted) }
                guard self.generation == captureGeneration, NSDictionary(dictionary:captureSignature).isEqual(to:self.captureContext()), self.processor.acceptLiveAnalysis(snapshot) else { return }
                self.telemetry.processing("poll",ms:elapsed)
                if var report { report["analysisMode"]="async-snapshot-v1";report["analysisDelayMs"]=PhoneClock.nowMs()-copyStart;self.emit(report) }
              }
            }
          }
        }
      }
    }
  }
}
