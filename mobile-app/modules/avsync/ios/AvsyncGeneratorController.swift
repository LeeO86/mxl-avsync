import AVFoundation
import UIKit

// Output scheduling stays entirely native. JS only opens/closes this screen.
final class AvsyncGeneratorController: UIViewController {
  var onClose: (([String: Any]) -> Void)?
  private let rate: Int
  private let taiUtc: Double
  private let sourceName: String
  private let subtitle: String
  private let sourceImage: UIImage?
  private let syncDot = CALayer()
  private let canvas = UIView()
  private let marker = CALayer()
  private var cells: [CALayer] = []
  private let status = UILabel()
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
  private var oldIdle = false
  private var outputLatency = 0.0
  private var lateFrames = 0
  private var lastFrame: UInt64?
  private var finishing = false
  private var sourceID = ""
  private var lastStatus = 0.0
  private var utcMinusHost = 0.0

  init(rate: Int, taiUtc: Double, name: String = "iPhone", subtitle: String = "", image: UIImage? = nil) {
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
    marker.backgroundColor = UIColor.black.cgColor
    canvas.layer.addSublayer(marker)
    canvas.layer.addSublayer(syncDot)
    // AVS2 carries identity in the QR; no per-frame strip layers.
    status.textColor = .lightGray; status.font = .systemFont(ofSize: 12); status.numberOfLines = 3
    status.textAlignment = .center; status.accessibilityIdentifier = "generatorStatus"
    view.addSubview(status)
    toggle.setTitle("Stop signal", for: .normal); toggle.addTarget(self, action: #selector(toggleSignal), for: .touchUpInside)
    toggle.accessibilityIdentifier = "generatorToggle"; view.addSubview(toggle)
    close.setTitle("Close", for: .normal); close.addTarget(self, action: #selector(closeScreen), for: .touchUpInside)
    close.accessibilityIdentifier = "generatorClose"; view.addSubview(close)
    engine.attach(player)
    engine.connect(player, to: engine.mainMixerNode, format: AVAudioFormat(standardFormatWithSampleRate: 48000, channels: 1)!)
    oldBrightness = UIScreen.main.brightness; oldIdle = UIApplication.shared.isIdleTimerDisabled
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
    do {
      let session = AVAudioSession.sharedInstance()
      try session.setCategory(.playback, mode: .default, options: [])
      try session.setPreferredSampleRate(48000)
      try session.setPreferredIOBufferDuration(0.005)
      try session.setActive(true)
      engine.prepare(); try engine.start()
      outputLatency = player.outputPresentationLatency
      guard outputLatency.isFinite, outputLatency >= 0, outputLatency < 1 else {
        throw NSError(domain: "Avsync", code: 20, userInfo: [NSLocalizedDescriptionKey: "Audio output latency is unavailable."])
      }
      plan = nil
      player.play()
      UIApplication.shared.isIdleTimerDisabled = true; UIScreen.main.brightness = 1
      let display = CADisplayLink(target: self, selector: #selector(tick(_:)))
      if #available(iOS 15.0, *) { display.preferredFrameRateRange = CAFrameRateRange(minimum: 60, maximum: 60, preferred: 60) }
      else { display.preferredFramesPerSecond = 60 }
      display.add(to: .main, forMode: .common); link = display
      toggle.setTitle("Stop signal", for: .normal)
      status.text = "Turn phone sideways · audible test signal starts shortly\n1200 baud · \(rate)/s · estimated source clock / output timing"
    } catch { stop("Cannot start: \(error.localizedDescription)") }
  }
  private func beginPlan(target: Double) throws {
      sourceID = "phone-" + UUID().uuidString.replacingOccurrences(of: "-", with: "").prefix(24)
      // Date and AV host time are sampled as a bracket. Preserve one mapping for
      // the whole session; stop if wall clock later jumps instead of rewriting it.
      let before = CACurrentMediaTime(), utc = Date().timeIntervalSince1970, after = CACurrentMediaTime()
      epochHost = target + 1.5
      utcMinusHost = utc - (before + after)/2
      let epochTai = Int64(((utc + taiUtc) + epochHost - (before + after)/2) * 1e9)
      plan = AVSyncGenerator(testID: sourceID, rate: rate, epochTaiNs: epochTai, epochHost: epochHost)
      guard let plan else { return }
      plan.setSource(name: sourceName, subtitle: subtitle, image: sourceImage)
      let geometry = plan.geometry()
      func n(_ key: String) -> CGFloat { CGFloat((geometry[key] as? NSNumber)?.doubleValue ?? 0) }
      marker.frame = CGRect(x: n("markerX"), y: n("markerY"), width: n("markerSide"), height: n("markerSide"))
      for (i, cell) in cells.enumerated() { cell.frame = CGRect(x: n("stripX") + CGFloat(i)*n("stripWidth"), y: n("stripY"), width: n("stripWidth"), height: n("stripHeight")) }
      nextAudio = 1; shown = 0; lateFrames = 0; lastFrame = nil; lastStatus = 0; images.removeAll()
      renderGeneration += 1
      let first = plan.image(sequence: 1); images[1] = first
      canvas.layer.contents = first.cgImage
      try fillAhead(now: after)
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
    if sequence != shown {
      guard let image = images[sequence] else { CATransaction.commit(); stop("Picture scheduling deadline missed. Restart the signal."); return }
      shown = sequence; canvas.layer.contents = image.cgImage
    }
    func n(_ key: String) -> CGFloat { CGFloat((frame[key] as? NSNumber)?.doubleValue ?? 0) }
    syncDot.isHidden = false
    syncDot.frame = CGRect(x: n("syncX"), y: n("syncY"), width: n("syncSize"), height: n("syncSize"))
    syncDot.backgroundColor = UIColor(red: n("syncR")/255, green: n("syncG")/255, blue: n("syncB")/255, alpha: 1).cgColor
    marker.backgroundColor = (frame["white"] as? Bool == true ? UIColor.white : UIColor.black).cgColor
    let bits = (frame["strip"] as? NSNumber)?.uint32Value ?? 0
    for (i, cell) in cells.enumerated() { cell.backgroundColor = (bits & (1 << (31-i)) != 0 ? UIColor.white : UIColor.black).cgColor }
    CATransaction.commit()
    if now - lastStatus > 0.5 {
      lastStatus = now
      if abs(Date().timeIntervalSince1970 - now - utcMinusHost) > 0.1 {
        stop("Phone clock changed. Tap Start to create a fresh clock mapping."); return
      }
      status.text = "TEST SIGNAL ACTIVE · 1200 baud · \(rate)/s · frame skips \(lateFrames)\nEstimated output timing · audio lead \(Int((outputLatency*1000).rounded())) ms"
    }
  }
  private func stop(_ message: String) {
    link?.invalidate(); link = nil; renderGeneration += 1
    player.stop(); engine.stop(); plan = nil; images.removeAll()
    CATransaction.begin(); CATransaction.setDisableActions(true)
    canvas.layer.contents = nil; syncDot.isHidden = true; marker.backgroundColor = UIColor.black.cgColor
    for cell in cells { cell.backgroundColor = UIColor.black.cgColor }
    CATransaction.commit()
    UIApplication.shared.isIdleTimerDisabled = oldIdle; UIScreen.main.brightness = oldBrightness
    try? AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)
    status.text = message; toggle.setTitle("Start signal", for: .normal)
  }
  @objc private func toggleSignal() { if link == nil { start() } else { stop("Signal stopped. Tap Start for a fresh source session.") } }
  func shutdown() { closeScreen() }
  @objc private func closeScreen() {
    guard !finishing else { return }; finishing = true
    let result: [String: Any] = ["source": sourceID, "eventRate": rate, "skippedDisplayFrames": lateFrames, "audioPresentationLatencyMs": outputLatency*1000, "estimated": true]
    stop("Closed")
    for token in notices { NotificationCenter.default.removeObserver(token) }; notices.removeAll()
    // Playback has stopped and cleanup is complete. Resolve independently of
    // UIKit's dismissal animation (which can outlive the presented controller).
    let callback = onClose; onClose = nil
    dismiss(animated: true)
    callback?(result)
  }
  override func viewDidDisappear(_ animated: Bool) { super.viewDidDisappear(animated); if !finishing { stop("Signal stopped") } }
  deinit { link?.invalidate(); for token in notices { NotificationCenter.default.removeObserver(token) } }
}
