import AVFoundation
import UIKit

// Output scheduling stays entirely native. JS only opens/closes this screen.
// With `calibrate` it shows the calibration pattern instead (docs/calibrator.md, display mode):
// the same display link, layers, audio engine and player node, plus the microphone.
final class AvsyncGeneratorController: UIViewController {
  var onClose: (([String: Any]) -> Void)?
  private let rate: Int
  private let taiUtc: Double
  private let sourceName: String
  private let subtitle: String
  private let sourceImage: UIImage?
  private let syncDot = CALayer()
  private let syncCenter = CALayer()
  private let syncForeground = CALayer()
  private let canvas = UIView()
  // AVS3 optical code: field A (left half of the panel) and B (right half), always opposite.
  private let markerA = CALayer(), markerB = CALayer()
  private var cells: [CALayer] = []
  private let status = UILabel()
  private let stoppedLabel = UILabel() // big "Stopped" over the picture while no signal runs
  private let toggle = UIButton(type: .system)
  private let close = UIButton(type: .system)
  private let engine = AVAudioEngine()
  private let player = AVAudioPlayerNode()
  private var link: CADisplayLink?
  private var plan: AVSyncGenerator?
  private var epochHost = 0.0
  private var nextAudio: UInt64 = 1
  private var shown: UInt64 = 0
  private var images: [UInt64: UIImage] = [:]
  private let imageWorker = DispatchQueue(label: "avsync.generator.images", qos: .userInitiated)
  private var renderGeneration = 0
  private var notices: [NSObjectProtocol] = []
  private var oldBrightness: CGFloat = 0
  private let screenAwake = ScreenAwakeLease()
  private let keepScreenAwake: Bool
  private var outputLatency = 0.0
  private var lateFrames = 0
  private var lastFrame: UInt64?
  private var finishing = false
  private var sourceID = ""
  private var lastStatus = 0.0
  private var utcMinusHost = 0.0
  private var planUtcMinusHost = 0.0 // the mapping the plan's stamps started from
  private var clockReference: [String:String] // the newest accepted clock check (updateClock)
  private var usingReference = false
  private var deviceNote = "" // why a signal on device UTC did not switch to a clock check
  private let senderCalibration: [String:String]
  private var senderCalibrationNote = ""
  // Calibration pattern: two halves, the calibrator outline, the microphone take.
  private let calibrate: Bool
  private let leftHalf = CALayer(), rightHalf = CALayer(), outline = CAShapeLayer()
  private let recordQueue = DispatchQueue(label: "avsync.generator.record")
  private var recorded: [Float] = [] // recordQueue
  private var recordRate = 48000.0
  private var nextBeep: UInt64 = 1
  private var analysing = false, lastAnalysis = 0.0
  private var calibrationResult: [String: Any]?
  private var calibrationError: String?
  private static let wantedPairs = 20, calibrationLimit = 30.0, pointsPerMm: CGFloat = 6 // about 6 pt per mm on current iPhones

  init(rate: Int, taiUtc: Double, name: String = "iPhone", subtitle: String = "", image: UIImage? = nil, clockReference: [String:String] = [:], keepScreenAwake: Bool = true,
       senderCalibration: [String:String] = [:], calibrate: Bool = false) {
    self.keepScreenAwake = keepScreenAwake
    self.clockReference = clockReference
    self.senderCalibration = senderCalibration
    self.calibrate = calibrate
    self.rate = rate; self.taiUtc = taiUtc; self.sourceName = name; self.subtitle = subtitle; self.sourceImage = image
    super.init(nibName: nil, bundle: nil)
    modalPresentationStyle = .fullScreen
  }
  required init?(coder: NSCoder) { fatalError("init(coder:) is unavailable") }
  override var prefersStatusBarHidden: Bool { true }
  override var prefersHomeIndicatorAutoHidden: Bool { true }
  override func viewDidLoad() {
    super.viewDidLoad()
    view.backgroundColor = .black
    canvas.backgroundColor = .black
    canvas.bounds = CGRect(x: 0, y: 0, width: 1280, height: 720)
    canvas.layer.magnificationFilter = .nearest
    view.addSubview(canvas)
    markerA.backgroundColor = UIColor.black.cgColor; markerB.backgroundColor = UIColor.white.cgColor
    canvas.layer.addSublayer(markerA); canvas.layer.addSublayer(markerB)
    canvas.layer.addSublayer(syncDot)
    syncCenter.backgroundColor = UIColor.black.cgColor
    syncDot.addSublayer(syncCenter)
    syncForeground.frame = canvas.bounds
    canvas.layer.addSublayer(syncForeground)
    // AVS3 carries identity in the QR; no per-frame strip layers.
    status.textColor = .lightGray; status.font = .systemFont(ofSize: 12); status.numberOfLines = 3
    status.textAlignment = .center; status.accessibilityIdentifier = "generatorStatus"
    view.addSubview(status)
    stoppedLabel.text = "Stopped"; stoppedLabel.textColor = .white; stoppedLabel.font = .boldSystemFont(ofSize: 34)
    stoppedLabel.textAlignment = .center; stoppedLabel.isHidden = true; stoppedLabel.layer.zPosition = 1
    stoppedLabel.accessibilityIdentifier = "generatorStopped"; view.addSubview(stoppedLabel)
    toggle.setTitle("Stop signal", for: .normal); toggle.addTarget(self, action: #selector(toggleSignal), for: .touchUpInside)
    toggle.accessibilityIdentifier = "generatorToggle"; view.addSubview(toggle)
    close.setTitle("Close", for: .normal); close.addTarget(self, action: #selector(closeScreen), for: .touchUpInside)
    close.accessibilityIdentifier = "generatorClose"; view.addSubview(close)
    if calibrate {
      canvas.isHidden = true
      leftHalf.backgroundColor = UIColor.white.cgColor; rightHalf.backgroundColor = UIColor.black.cgColor
      outline.strokeColor = UIColor.systemRed.cgColor; outline.fillColor = nil; outline.lineWidth = 2
      for layer in [leftHalf, rightHalf, outline] { view.layer.addSublayer(layer) }
      toggle.setTitle("Stop", for: .normal)
    }
    engine.attach(player)
    engine.connect(player, to: engine.mainMixerNode, format: AVAudioFormat(standardFormatWithSampleRate: 48000, channels: 1)!)
    oldBrightness = UIScreen.main.brightness
    notices.append(NotificationCenter.default.addObserver(forName: UIApplication.willResignActiveNotification, object: nil, queue: .main) { [weak self] _ in self?.stop("Paused: app is inactive. Tap Start to create a fresh source session.") })
    notices.append(NotificationCenter.default.addObserver(forName: AVAudioSession.interruptionNotification, object: nil, queue: .main) { [weak self] _ in self?.stop("Audio interrupted. Tap Start to restart.") })
    notices.append(NotificationCenter.default.addObserver(forName: AVAudioSession.routeChangeNotification, object: nil, queue: .main) { [weak self] note in
      guard let raw = note.userInfo?[AVAudioSessionRouteChangeReasonKey] as? UInt,
            let value = AVAudioSession.RouteChangeReason(rawValue: raw),
            value != .categoryChange, value != .override else { return }
      guard self?.plan != nil else { return }
      self?.stop("Audio output changed. Tap Start to re-plan timing.")
    })
    notices.append(NotificationCenter.default.addObserver(forName: .AVAudioEngineConfigurationChange, object: engine, queue: .main) { [weak self] _ in
      guard self?.plan != nil else { return }
      self?.stop("Audio configuration changed. Tap Start to restart.")
    })
  }
  override func viewDidAppear(_ animated: Bool) { super.viewDidAppear(animated); start() }
  override func viewDidLayoutSubviews() {
    super.viewDidLayoutSubviews()
    stoppedLabel.frame = CGRect(x: 0, y: view.bounds.midY - 30, width: view.bounds.width, height: 60)
    if calibrate { layoutCalibration(); return }
    // Keep capture screens portrait. Only this test raster rotates; holding the
    // phone sideways gives a large landscape target without changing camera UI.
    let inset = view.safeAreaInsets
    let available = CGSize(width: view.bounds.width - 24, height: view.bounds.height - inset.top - inset.bottom - 105)
    let portrait = available.height > available.width
    let scale = min((portrait ? available.height : available.width) / 1280, (portrait ? available.width : available.height) / 720)
    canvas.transform = CGAffineTransform(rotationAngle: portrait ? .pi / 2 : 0).scaledBy(x: scale, y: scale)
    canvas.center = CGPoint(x: view.bounds.midX, y: inset.top + available.height / 2)
    status.frame = CGRect(x: 12, y: view.bounds.height - inset.bottom - 100, width: view.bounds.width - 24, height: 52)
    toggle.frame = CGRect(x: 16, y: view.bounds.height - inset.bottom - 45, width: view.bounds.width/2 - 24, height: 40)
    close.frame = CGRect(x: view.bounds.width/2 + 8, y: toggle.frame.minY, width: view.bounds.width/2 - 24, height: 40)
  }
  private func start() {
    guard link == nil, !finishing else { return }
    stoppedLabel.isHidden = true
    do {
      let session = AVAudioSession.sharedInstance()
      // Calibration also records: play-and-record, still on the built-in speaker (not the receiver).
      if calibrate { try session.setCategory(.playAndRecord, mode: .default, options: [.defaultToSpeaker]) }
      else { try session.setCategory(.playback, mode: .default, options: []) }
      try session.setPreferredSampleRate(48000)
      try session.setPreferredIOBufferDuration(0.005)
      try session.setActive(true)
      if calibrate { try startRecording() }
      engine.prepare(); try engine.start()
      outputLatency = player.outputPresentationLatency
      guard outputLatency.isFinite, outputLatency >= 0, outputLatency < 1 else {
        throw NSError(domain: "Avsync", code: 20, userInfo: [NSLocalizedDescriptionKey: "Audio output latency is unavailable."])
      }
      plan = nil
      player.play()
      screenAwake.setActive(keepScreenAwake); UIScreen.main.brightness = 1
      let display = CADisplayLink(target: self, selector: #selector(tick(_:)))
      if #available(iOS 15.0, *) { display.preferredFrameRateRange = CAFrameRateRange(minimum: 60, maximum: 60, preferred: 60) }
      else { display.preferredFramesPerSecond = 60 }
      display.add(to: .main, forMode: .common); link = display
      toggle.setTitle(calibrate ? "Stop" : "Stop signal", for: .normal)
      status.text = calibrate ? "Lay the calibrator on the red outline, sensors over the line. Keep the volume up."
        : "Turn phone sideways · audible test signal starts shortly\n1200 baud · \(rate)/s · estimated source clock / output timing"
    } catch { stop("Cannot start: \(error.localizedDescription)") }
  }
  private func beginPlan(target: Double) throws {
      sourceID = Self.stableSourceID(rate: rate)
      // Date and AV host time are sampled as a bracket. Preserve one mapping for
      // the whole session; stop if wall clock later jumps instead of rewriting it.
      let before = CACurrentMediaTime(), utc = Date().timeIntervalSince1970, after = CACurrentMediaTime()
      epochHost = target + 1.5
      utcMinusHost = utc - (before + after)/2
      var referenceUtcMinusHost = utcMinusHost
      usingReference = false
      if let mapping = referenceMapping(clockReference, now: before) {
        referenceUtcMinusHost = mapping
        usingReference = true
      }
      planUtcMinusHost = referenceUtcMinusHost
      deviceNote = ""
      let epochTai = Int64((referenceUtcMinusHost + taiUtc + epochHost) * 1e9)
      plan = AVSyncGenerator(testID: sourceID, rate: rate, epochTaiNs: epochTai, epochHost: epochHost)
      guard let plan else { return }
      plan.setSource(name: sourceName, subtitle: subtitle, image: sourceImage)
      applySenderCalibration(plan)
      syncForeground.contents = plan.syncForeground().cgImage
      let geometry = plan.geometry()
      func n(_ key: String) -> CGFloat { CGFloat((geometry[key] as? NSNumber)?.doubleValue ?? 0) }
      let half = (n("markerSide") / 2).rounded(.down)
      markerA.frame = CGRect(x: n("markerX"), y: n("markerY"), width: half, height: n("markerSide"))
      markerB.frame = CGRect(x: n("markerX") + half, y: n("markerY"), width: n("markerSide") - half, height: n("markerSide"))
      for (i, cell) in cells.enumerated() { cell.frame = CGRect(x: n("stripX") + CGFloat(i)*n("stripWidth"), y: n("stripY"), width: n("stripWidth"), height: n("stripHeight")) }
      nextAudio = 1; shown = 0; lateFrames = 0; lastFrame = nil; lastStatus = 0; images.removeAll()
      renderGeneration += 1
      let first = plan.image(sequence: 1); images[1] = first
      canvas.layer.contents = first.cgImage
      try fillAhead(now: after)
  }
  // UTC minus host seconds of a clock check on this phone's clock; nil for another clock or when not 0–300 s old.
  private func referenceMapping(_ r: [String:String], now: Double) -> Double? {
    guard r["clockId"] == PhoneClock.identifier,
          let unixMs = Double(r["clockUnixMs"] ?? ""), (1e12...4e12).contains(unixMs),
          let hostMs = Double(r["clockMonotonicMs"] ?? ""), hostMs.isFinite,
          now*1000-hostMs >= 0, now*1000-hostMs < 300_000 else { return nil }
    return (unixMs-hostMs)/1000
  }
  // A newer clock check from the app (docs/phone-clock.md). Before the plan starts (or while stopped)
  // it replaces the reference for the next start. While the signal runs, the stamps of the events not
  // scheduled yet slew to it (core retime, 100 ppm); host times and the Test ID stay. A signal on
  // device UTC switches to NTP UTC this way. A check further from the current stamps than the limit
  // stops a signal on NTP UTC; one on device UTC keeps running and says so in the status line.
  func updateClock(_ reference: [String:String]) {
    let now = CACurrentMediaTime()
    guard !calibrate, let mapping = referenceMapping(reference, now: now),
          let anchor = Double(reference["clockMonotonicMs"] ?? ""), let radius = Double(reference["clockUncertaintyMs"] ?? ""),
          let limit = Double(reference["clockLimitMs"] ?? ""),
          !AVSyncGenerator.clockReferenceExpired(ageMs: now*1000-anchor, radiusMs: radius, limitMs: limit) else { return }
    guard let plan else { clockReference = reference; return }
    let offset = mapping - planUtcMinusHost
    if plan.retime(offsetNs: Int64((offset*1e9).rounded()), fromSequence: nextAudio, maxStepNs: Int64((limit*1e6).rounded())) {
      clockReference = reference; usingReference = true; deviceNote = ""; return
    }
    let change = offset*1000 - plan.clockOffsetMs(sequence: nextAudio)
    guard usingReference else {
      deviceNote = String(format: " (clock check %+.1f ms off, more than the %.0f ms limit)", change, limit); return
    }
    clockReference = reference
    stop(String(format: "New clock check is %+.1f ms off (more than the %.0f ms limit). Tap Start for a fresh clock mapping.", change, limit))
  }
  // Same install, app version (with build number) and timing settings give the same ID, so a restart keeps it.
  // The install ID is random, kept in UserDefaults and gone after a reinstall.
  // Name, subtitle and image are cosmetic and stay out. Add a new timing setting to `settings`.
  private static func stableSourceID(rate: Int) -> String {
    let defaults = UserDefaults.standard, key = "avsync.generator.installId"
    let install = defaults.string(forKey: key) ?? UUID().uuidString
    defaults.set(install, forKey: key)
    let info = Bundle.main.infoDictionary ?? [:]
    let version = "\(info["CFBundleShortVersionString"] as? String ?? "") (\(info["CFBundleVersion"] as? String ?? ""))"
    let settings = ["rate=\(rate)"]
    return AVSyncGenerator.testID(prefix: "phone-", installID: install, version: version, settings: settings)
  }
  private func fillAhead(now: Double) throws {
    guard let plan else { return }
    while plan.hostTime(sequence: nextAudio) < now + 2.5 {
      let host = plan.hostTime(sequence: nextAudio) - outputLatency
      guard host > CACurrentMediaTime() + 0.03 else {
        throw NSError(domain: "Avsync", code: 21, userInfo: [NSLocalizedDescriptionKey: "Scheduling deadline missed. Restart the signal."])
      }
      let data = plan.audio(sequence: nextAudio)
      let format = AVAudioFormat(standardFormatWithSampleRate: 48000, channels: 1)!
      let count = AVAudioFrameCount(data.count / MemoryLayout<Float>.size)
      let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: count)!
      buffer.frameLength = count
      _ = data.withUnsafeBytes { bytes in memcpy(buffer.floatChannelData![0], bytes.baseAddress!, data.count) }
      player.scheduleBuffer(buffer, at: AVAudioTime(hostTime: AVAudioTime.hostTime(forSeconds: host)), options: [])
      if images[nextAudio] == nil {
        let sequence = nextAudio, generation = renderGeneration
        imageWorker.async { [weak self] in
          let image = plan.image(sequence: sequence)
          DispatchQueue.main.async { [weak self] in
            guard let self, self.renderGeneration == generation, self.plan === plan else { return }
            self.images[sequence] = image
          }
        }
      }
      nextAudio += 1
    }
    images = images.filter { $0.key + 1 >= shown }
  }
  @objc private func tick(_ display: CADisplayLink) {
    guard link != nil else { return }
    guard engine.isRunning else { stop("Audio engine stopped. Tap Start to restart."); return }
    if calibrate { calibrationTick(display); return }
    if plan == nil {
      do { try beginPlan(target: display.targetTimestamp) } catch { stop(error.localizedDescription); return }
    }
    guard let plan else { return }
    let now = CACurrentMediaTime()
    guard now - epochHost < 23*3600 else { stop("Session complete. Tap Start for a new source."); return }
    do { try fillAhead(now: now) } catch { stop(error.localizedDescription); return }
    guard display.targetTimestamp >= epochHost else { return }
    let frame = plan.frame(at: display.targetTimestamp)
    let sequence = (frame["sequence"] as? NSNumber)?.uint64Value ?? 1
    let position = (frame["position"] as? NSNumber)?.uint64Value ?? 0
    let current = (sequence - 1) * UInt64(60/rate) + position
    if let previous = lastFrame, current > previous + 1 { lateFrames += Int(current - previous - 1) }
    lastFrame = current
    CATransaction.begin(); CATransaction.setDisableActions(true)
    // The QR names the next event from half a period before its burst.
    let named = (frame["named"] as? NSNumber)?.uint64Value ?? sequence
    if named != shown {
      guard let image = images[named] else { CATransaction.commit(); stop("Picture scheduling deadline missed. Restart the signal."); return }
      shown = named; canvas.layer.contents = image.cgImage
    }
    func n(_ key: String) -> CGFloat { CGFloat((frame[key] as? NSNumber)?.doubleValue ?? 0) }
    syncDot.isHidden = false
    syncDot.frame = CGRect(x: n("syncX"), y: n("syncY"), width: n("syncSize"), height: n("syncSize"))
    syncCenter.frame = CGRect(x: (n("syncSize")-n("syncLineWidth"))/2, y: 0, width: n("syncLineWidth"), height: n("syncSize"))
    syncDot.backgroundColor = UIColor(red: n("syncR")/255, green: n("syncG")/255, blue: n("syncB")/255, alpha: 1).cgColor
    let aWhite = frame["white"] as? Bool == true
    markerA.backgroundColor = (aWhite ? UIColor.white : UIColor.black).cgColor
    markerB.backgroundColor = (aWhite ? UIColor.black : UIColor.white).cgColor
    let bits = (frame["strip"] as? NSNumber)?.uint32Value ?? 0
    for (i, cell) in cells.enumerated() { cell.backgroundColor = (bits & (1 << (31-i)) != 0 ? UIColor.white : UIColor.black).cgColor }
    CATransaction.commit()
    if now - lastStatus > 0.5 {
      lastStatus = now
      // Expires when no newer clock check arrived in time (updateClock), by the rule of the app.
      var clockNote = ""
      if usingReference, let anchor = Double(clockReference["clockMonotonicMs"] ?? ""),
         let radius = Double(clockReference["clockUncertaintyMs"] ?? ""), let limit = Double(clockReference["clockLimitMs"] ?? "") {
        if AVSyncGenerator.clockReferenceExpired(ageMs: now*1000-anchor, radiusMs: radius, limitMs: limit) {
          stop("Clock reference expired. Close and synchronize again."); return
        }
        let remaining = plan.clockOffsetMs(sequence: UInt64.max) - plan.clockOffsetMs(sequence: sequence)
        clockNote = " (check \(Int(now - anchor/1000)) s ago" + (abs(remaining) >= 0.05 ? String(format: ", adjusting %+.1f ms", remaining) : "") + ")"
      }
      if abs(Date().timeIntervalSince1970 - now - utcMinusHost) > 0.1 {
        stop("Phone clock changed. Tap Start to create a fresh clock mapping."); return
      }
      status.text = "TEST SIGNAL ACTIVE · 1200 baud · \(rate)/s · frame skips \(lateFrames)\nEstimated output timing · \(usingReference ? "NTP UTC\(clockNote)" : "device UTC\(deviceNote)") · audio lead \(Int((outputLatency*1000).rounded())) ms\(senderCalibrationNote)"
    }
  }
  private func stop(_ message: String) {
    link?.invalidate(); link = nil; renderGeneration += 1
    if calibrate { engine.inputNode.removeTap(onBus: 0) }
    player.stop(); engine.stop(); plan = nil; images.removeAll()
    CATransaction.begin(); CATransaction.setDisableActions(true)
    // Clear the whole raster (scale and marker too), so no stale test picture stays on screen.
    canvas.layer.contents = nil; syncForeground.contents = nil; syncDot.isHidden = true
    markerA.backgroundColor = UIColor.black.cgColor; markerB.backgroundColor = UIColor.black.cgColor
    stoppedLabel.isHidden = false
    for cell in cells { cell.backgroundColor = UIColor.black.cgColor }
    CATransaction.commit()
    screenAwake.setActive(false); UIScreen.main.brightness = oldBrightness
    try? AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)
    status.text = message; toggle.setTitle(calibrate ? "Start" : "Start signal", for: .normal)
  }
  @objc private func toggleSignal() { if link == nil { start() } else { stop("Signal stopped. Tap Start for a fresh source session.") } }
  func shutdown() { closeScreen() }
  @objc private func closeScreen() {
    guard !finishing else { return }; finishing = true
    var result: [String: Any] = ["source": sourceID, "eventRate": rate, "skippedDisplayFrames": lateFrames, "audioPresentationLatencyMs": outputLatency*1000, "estimated": true, "clockReference": clockReference, "clockApplied": usingReference]
    if let calibrationResult { result["calibration"] = calibrationResult }
    if let calibrationError { result["calibrationError"] = calibrationError }
    stop("Closed")
    for token in notices { NotificationCenter.default.removeObserver(token) }; notices.removeAll()
    // Playback has stopped and cleanup is complete. Resolve independently of
    // UIKit's dismissal animation (which can outlive the presented controller).
    let callback = onClose; onClose = nil
    dismiss(animated: true)
    callback?(result)
  }
  override func viewDidDisappear(_ animated: Bool) { super.viewDidDisappear(animated); if !finishing { stop("Signal stopped") } }
  // MARK: sender calibration and calibration pattern

  static func outputRoute() -> String {
    AVAudioSession.sharedInstance().currentRoute.outputs.map { $0.portType.rawValue }.sorted().joined(separator: "+")
  }
  // The saved screen-vs-speaker calibration goes into the AVS4 QR, but only on the audio output it was measured on.
  private func applySenderCalibration(_ plan: AVSyncGenerator) {
    senderCalibrationNote = ""
    guard let method = senderCalibration["senderCalibrationMethod"], let offset = Double(senderCalibration["senderCalibrationOffsetMs"] ?? ""),
          let uncertainty = Double(senderCalibration["senderCalibrationUncertaintyMs"] ?? ""), let day = Int(senderCalibration["senderCalibrationDay"] ?? "") else { return }
    if let route = senderCalibration["senderCalibrationRoute"], route != Self.outputRoute() {
      senderCalibrationNote = " · sender calibration not sent (other audio output)"; return
    }
    plan.setSenderCalibration(method: method, offsetMs: offset, uncertaintyMs: uncertainty, day: day)
    senderCalibrationNote = String(format: " · sender calibration %+.1f ms", offset)
  }
  private func startRecording() throws {
    let input = engine.inputNode, format = input.outputFormat(forBus: 0)
    guard format.sampleRate > 0, format.channelCount > 0 else {
      throw NSError(domain: "Avsync", code: 22, userInfo: [NSLocalizedDescriptionKey: "The microphone is not available. Allow microphone access in Settings."])
    }
    recordRate = format.sampleRate
    recordQueue.sync { recorded.removeAll() }
    input.removeTap(onBus: 0)
    input.installTap(onBus: 0, bufferSize: 4800, format: format) { [weak self] buffer, _ in
      guard let data = buffer.floatChannelData?[0] else { return }
      let samples = Array(UnsafeBufferPointer(start: data, count: Int(buffer.frameLength)))
      self?.recordQueue.async { self?.recorded.append(contentsOf: samples) }
    }
  }
  // Portrait, halves left and right (the pattern line crosses the display rows), near the
  // microphone end at the bottom. The outline is the 66 x 22 mm board, sensor end down.
  private func layoutCalibration() {
    let inset = view.safeAreaInsets, w = view.bounds.width, h = view.bounds.height, mm = Self.pointsPerMm
    toggle.frame = CGRect(x: 16, y: inset.top + 8, width: w/2 - 24, height: 40)
    close.frame = CGRect(x: w/2 + 8, y: toggle.frame.minY, width: w/2 - 24, height: 40)
    status.frame = CGRect(x: 12, y: toggle.frame.maxY + 4, width: w - 24, height: 52)
    let top = max(status.frame.maxY + 8, h - 62 * mm)
    CATransaction.begin(); CATransaction.setDisableActions(true)
    leftHalf.frame = CGRect(x: 0, y: top, width: w/2, height: h - top)
    rightHalf.frame = CGRect(x: w/2, y: top, width: w - w/2, height: h - top)
    // Sensors (5 mm from the notch end) at the middle of the pattern.
    let notch = (top + h) / 2 + 5 * mm
    let board = UIBezierPath(roundedRect: CGRect(x: w/2 - 11 * mm, y: notch - 66 * mm, width: 22 * mm, height: 66 * mm), cornerRadius: 1.5 * mm)
    board.move(to: CGPoint(x: w/2 - 1.5 * mm, y: notch + 2 * mm)); board.addLine(to: CGPoint(x: w/2 + 1.5 * mm, y: notch + 2 * mm)) // notch tick
    outline.path = board.cgPath
    CATransaction.commit()
  }
  private func swapHost(_ k: UInt64) -> Double { epochHost + Double(AVSyncGenerator.calibrationSwapFrame(swap: k, fps: 60)) / 60 }
  private func calibrationTick(_ display: CADisplayLink) {
    let now = CACurrentMediaTime()
    if plan == nil {
      // The plan only marks a running session here (route and configuration changes stop it).
      epochHost = display.targetTimestamp + 1.0
      plan = AVSyncGenerator(testID: "calibration", rate: 2, epochTaiNs: 0, epochHost: epochHost)
      nextBeep = 1; lateFrames = 0; lastFrame = nil; lastAnalysis = now; calibrationResult = nil; calibrationError = nil
    }
    // The beep: D = 100 ms after each swap on the same host clock, minus the reported output latency.
    while swapHost(nextBeep) + 0.1 < now + 2.5 {
      let host = swapHost(nextBeep) + 0.1 - outputLatency
      guard host > now + 0.03 else { stop("Scheduling deadline missed. Tap Start to try again."); return }
      let data = AVSyncGenerator.calibrationBeep(sampleRate: 48000)
      let format = AVAudioFormat(standardFormatWithSampleRate: 48000, channels: 1)!
      let count = AVAudioFrameCount(data.count / MemoryLayout<Float>.size)
      let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: count)!
      buffer.frameLength = count
      _ = data.withUnsafeBytes { bytes in memcpy(buffer.floatChannelData![0], bytes.baseAddress!, data.count) }
      player.scheduleBuffer(buffer, at: AVAudioTime(hostTime: AVAudioTime.hostTime(forSeconds: host)), options: [])
      nextBeep += 1
    }
    let position = (display.targetTimestamp - epochHost) * 60
    let frame = UInt64(max(0, position.rounded()))
    if position > -0.5 {
      if let previous = lastFrame, frame > previous + 1 { lateFrames += Int(frame - previous - 1) }
      lastFrame = frame
    }
    let white = position < -0.5 || AVSyncGenerator.calibrationLeftWhite(frame: frame, fps: 60)
    CATransaction.begin(); CATransaction.setDisableActions(true)
    leftHalf.backgroundColor = (white ? UIColor.white : UIColor.black).cgColor
    rightHalf.backgroundColor = (white ? UIColor.black : UIColor.white).cgColor
    CATransaction.commit()
    guard !analysing, now - lastAnalysis >= 1, now > epochHost + 1.5 else { return }
    lastAnalysis = now; analysing = true
    let running = now - epochHost, rate = recordRate, skips = lateFrames, latency = outputLatency
    recordQueue.async { [weak self] in
      guard let self else { return }
      let data = self.recorded.withUnsafeBufferPointer { Data(buffer: $0) }
      let progress = AVSyncGenerator.displayCalibration(samples: data, sampleRate: rate, minimumPairs: 1)
      let pairs = (progress["pairs"] as? NSNumber)?.intValue ?? 0, clicks = (progress["clicks"] as? NSNumber)?.intValue ?? 0
      let done = pairs >= Self.wantedPairs || running >= Self.calibrationLimit
      let final = done ? AVSyncGenerator.displayCalibration(samples: data, sampleRate: rate, minimumPairs: 8) : progress
      DispatchQueue.main.async { [weak self] in
        guard let self else { return }
        self.analysing = false
        guard self.link != nil else { return }
        if !done {
          self.status.text = clicks == 0 && running > 3 ? "No click yet. Lay the calibrator on the red outline, sensors over the line, and check the cable."
            : "Measuring · \(pairs) of \(Self.wantedPairs) swaps · frame skips \(skips)"
          return
        }
        if (final["ok"] as? Bool) == true {
          var result = final
          result["method"] = "calibrator-display"; result["audioRoute"] = Self.outputRoute(); result["outputLatencyMs"] = latency * 1000
          result["skippedDisplayFrames"] = skips; result["sampleRate"] = rate
          self.calibrationResult = result
          self.closeScreen()
        } else {
          self.calibrationError = final["error"] as? String
          self.stop(self.calibrationError ?? "Calibration failed. Tap Start to try again.")
        }
      }
    }
  }
  deinit { link?.invalidate(); for token in notices { NotificationCenter.default.removeObserver(token) } }
}
