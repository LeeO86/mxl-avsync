package io.leeo86.mxl.avsync

import android.Manifest
import android.app.Activity
import android.app.Dialog
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.media.AudioManager
import android.os.Build
import android.os.SystemClock
import android.util.TypedValue
import android.view.Choreographer
import android.view.Gravity
import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.view.WindowManager
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import androidx.annotation.RequiresApi
import org.json.JSONObject
import java.util.UUID
import java.util.concurrent.Executors
import kotlin.math.abs
import kotlin.math.roundToLong

// The fullscreen test signal (docs/phone-generator.md), like ios/AvsyncGeneratorController.swift.
// Output scheduling stays native; JS only opens this screen and gets the result when it closes.
// Frames: Choreographer gives each frame's expected presentation time (System.nanoTime, the clock of
// the audio timestamps too); the 60 Hz plan says what that frame shows. Sound: GeneratorAudio.
// With `calibrate` it shows the calibration pattern instead (docs/calibrator.md, display mode): the
// same frame clock, audio output and scheduling, plus the microphone.
class AvsyncGenerator(private val activity: Activity, private val options: Options, private val onClose: (Map<String, Any>) -> Unit) :
  Dialog(activity, android.R.style.Theme_Black_NoTitleBar_Fullscreen) {

  class Options(val rate: Int, val taiUtc: Double, val name: String, val subtitle: String, val image: Bitmap?,
    val clockReference: Map<String, String>, val keepScreenAwake: Boolean, val senderCalibration: Map<String, String>, val calibrate: Boolean)

  private val rate = options.rate
  private val calibrate = options.calibrate
  private val raster = Raster(context)
  private val pattern = CalibrationPattern(context)
  private val status = TextView(context)
  private val stoppedLabel = TextView(context) // big "Stopped" over the picture while no signal runs
  private val toggle = Button(context)
  private val close = Button(context)
  private val audio = GeneratorAudio { message -> stop(message) }
  private val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
  private val choreographer = Choreographer.getInstance()
  private val imageWorker = Executors.newSingleThreadExecutor { Thread(it, "avsync.generator.images") }
  private var ticking = false
  private var session = 0 // one frame-callback chain per start; a callback of an older start ends its chain
  private var finishing = false
  private var plan: PhoneGenerator? = null
  private var epochHost = -1.0 // host seconds (System.nanoTime) of frame 0; -1 while no session runs
  private var startedNs = 0L
  private var vsyncNs = 16_666_667L
  private var nextAudio = 1L
  private var shown = 0L
  private val images = HashMap<Long, Bitmap>()
  private var generation = 0
  private var outputLatency = 0.0
  private var lateFrames = 0
  private var lastFrame: Long? = null
  private var sourceID = ""
  private var lastStatus = 0.0
  private var utcMinusHost = 0.0
  private var planUtcMinusHost = 0.0 // the mapping the plan's stamps started from
  private var clockReference = options.clockReference // the newest accepted clock check (updateClock)
  private var usingReference = false
  private var senderCalibrationNote = ""
  // Calibration pattern: the microphone take.
  private var recorder: AudioCapture? = null
  private val recordLock = Any()
  private var recorded = FloatArray(0) // recordLock
  private var recordedCount = 0        // recordLock
  private var nextBeep = 1L
  private var analysing = false
  private var lastAnalysis = 0.0
  private val analysisWorker = Executors.newSingleThreadExecutor { Thread(it, "avsync.generator.analysis") }
  private var calibrationResult: Map<String, Any>? = null
  private var calibrationError: String? = null

  init {
    GeneratorText.load(context)
    setCancelable(true)
    setOnCancelListener { closeScreen() }
    setOnDismissListener { if (!finishing) closeScreen() }
    val root = FrameLayout(context).apply { setBackgroundColor(Color.BLACK) }
    val content = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }
    status.setTextColor(Color.LTGRAY); status.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f); status.gravity = Gravity.CENTER; status.setLines(3)
    status.contentDescription = "generatorStatus"
    stoppedLabel.text = "Stopped"; stoppedLabel.setTextColor(Color.WHITE); stoppedLabel.setTextSize(TypedValue.COMPLEX_UNIT_SP, 34f)
    stoppedLabel.paint.isFakeBoldText = true; stoppedLabel.gravity = Gravity.CENTER; stoppedLabel.visibility = View.GONE
    toggle.text = if (calibrate) "Stop" else "Stop signal"; toggle.setOnClickListener { if (ticking) stop("Signal stopped. Tap Start for a fresh source session.") else start() }
    close.text = "Close"; close.setOnClickListener { closeScreen() }
    val buttons = LinearLayout(context).apply {
      orientation = LinearLayout.HORIZONTAL
      addView(toggle, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f)); addView(close, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
    }
    val pad = dp(12)
    if (calibrate) {
      content.addView(buttons, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply { setMargins(pad, pad, pad, 0) })
      content.addView(status, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply { setMargins(pad, 0, pad, 0) })
      content.addView(pattern, LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f))
    } else {
      // Keep capture screens portrait. Only this test raster rotates; holding the phone sideways
      // gives a large landscape target without changing camera UI.
      content.addView(raster, LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f).apply { setMargins(pad, pad, pad, 0) })
      content.addView(status, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply { setMargins(pad, 0, pad, 0) })
      content.addView(buttons, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply { setMargins(pad, 0, pad, pad) })
    }
    root.addView(content, FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT))
    root.addView(stoppedLabel, FrameLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT, Gravity.CENTER))
    setContentView(root)
    window?.let { w ->
      w.setLayout(MATCH_PARENT, MATCH_PARENT)
      @Suppress("DEPRECATION")
      w.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_FULLSCREEN or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
        View.SYSTEM_UI_FLAG_LAYOUT_STABLE or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
      // A 60 Hz display mode at the current resolution when the display has one (the plan's frame grid).
      @Suppress("DEPRECATION") val display = activity.windowManager.defaultDisplay
      val current = display.mode
      display.supportedModes.filter { it.physicalWidth == current.physicalWidth && it.physicalHeight == current.physicalHeight }
        .minByOrNull { abs(it.refreshRate - 60f) }?.let { mode -> w.attributes = w.attributes.apply { preferredDisplayModeId = mode.modeId } }
    }
    setOnShowListener { start() }
  }

  private fun dp(value: Int) = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value.toFloat(), context.resources.displayMetrics).toInt()
  private fun nowSeconds() = System.nanoTime() / 1e9

  // ---- Start, stop, close ----

  @Suppress("DEPRECATION") // requestAudioFocus(listener, stream, duration): the only form on API 24–25.
  private fun start() {
    if (ticking || finishing) return
    stoppedLabel.visibility = View.GONE
    try {
      audioManager.requestAudioFocus(focus, AudioManager.STREAM_MUSIC, AudioManager.AUDIOFOCUS_GAIN_TRANSIENT)
      if (calibrate) startRecording()
      audio.start { if (plan != null || epochHost >= 0) stop("Audio output changed. Tap Start to re-plan timing.") }
      plan = null; epochHost = -1.0; startedNs = System.nanoTime()
      @Suppress("DEPRECATION") val display = activity.windowManager.defaultDisplay
      vsyncNs = (1e9 / display.refreshRate.coerceIn(24f, 240f)).toLong()
      window?.let { w ->
        w.attributes = w.attributes.apply { screenBrightness = 1f }
        if (options.keepScreenAwake) w.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
      }
      ticking = true; session++; postTick(session)
      toggle.text = if (calibrate) "Stop" else "Stop signal"
      status.text = if (calibrate) "Lay the calibrator on the red outline, sensors over the line. Keep the volume up."
        else "Turn phone sideways · audible test signal starts shortly\n1200 baud · $rate/s · estimated source clock / output timing"
    } catch (e: Exception) { stop("Cannot start: ${e.message}") }
  }

  @Suppress("DEPRECATION")
  private val focus = AudioManager.OnAudioFocusChangeListener { change ->
    if (change == AudioManager.AUDIOFOCUS_LOSS || change == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT) stop("Audio interrupted. Tap Start to restart.")
  }

  @Suppress("DEPRECATION")
  fun stop(message: String) {
    ticking = false; generation++
    recorder?.stop(); recorder = null
    audio.stop()
    plan?.let { old -> imageWorker.execute { old.close() } } // after the renders already queued
    plan = null; epochHost = -1.0; images.clear()
    // Clear the whole raster (scale and marker too), so no stale test picture stays on screen.
    raster.clear()
    stoppedLabel.visibility = View.VISIBLE
    window?.let { w ->
      w.attributes = w.attributes.apply { screenBrightness = WindowManager.LayoutParams.BRIGHTNESS_OVERRIDE_NONE }
      w.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    }
    audioManager.abandonAudioFocus(focus)
    status.text = message; toggle.text = if (calibrate) "Start" else "Start signal"
  }

  // The app went to the background (module lifecycle).
  fun pause() { if (ticking) stop("Paused: app is inactive. Tap Start to create a fresh source session.") }
  fun shutdown() = closeScreen()

  private fun closeScreen() {
    if (finishing) return
    finishing = true
    val result = mutableMapOf<String, Any>("source" to sourceID, "eventRate" to rate, "skippedDisplayFrames" to lateFrames,
      "audioPresentationLatencyMs" to outputLatency * 1000, "estimated" to true, "clockReference" to clockReference, "clockApplied" to usingReference)
    calibrationResult?.let { result["calibration"] = it }
    calibrationError?.let { result["calibrationError"] = it }
    stop("Closed")
    imageWorker.shutdown(); analysisWorker.shutdown()
    dismiss()
    onClose(result)
  }

  // ---- Frame clock ----

  // Before Android 13 there is no expected presentation time: a frame started at vsync N is shown about two vsyncs later.
  private fun postTick(chain: Int) {
    if (Build.VERSION.SDK_INT >= 33) Vsync33.post(choreographer) { tick(it, chain) }
    else choreographer.postFrameCallback { frameNs -> tick(frameNs + 2 * vsyncNs, chain) }
  }

  @RequiresApi(33)
  private object Vsync33 {
    fun post(choreographer: Choreographer, tick: (Long) -> Unit) =
      choreographer.postVsyncCallback { data -> tick(data.preferredFrameTimeline.expectedPresentationTimeNanos) }
  }

  // targetNs: when the frame drawn now is expected on screen (System.nanoTime).
  private fun tick(targetNs: Long, chain: Int) {
    if (!ticking || chain != session) return
    postTick(chain)
    val target = targetNs / 1e9
    if (calibrate) return calibrationTick(target)
    if (plan == null) {
      // The plan starts once the output reports its timing (iOS: outputPresentationLatency).
      val lead = audio.latencyNs
      if (lead !in 0 until 1_000_000_000L) {
        if (System.nanoTime() - startedNs > 3_000_000_000L) stop("Audio output latency is unavailable.")
        return
      }
      try { beginPlan(target) } catch (e: Exception) { return stop(e.message ?: "Cannot start the signal.") }
    }
    val plan = plan ?: return
    val now = nowSeconds()
    if (now - epochHost >= 23 * 3600) return stop("Session complete. Tap Start for a new source.")
    try { fillAhead(now) } catch (e: Exception) { return stop(e.message ?: "Scheduling deadline missed. Restart the signal.") }
    if (target < epochHost) return
    val frame = plan.frame(target)
    val current = (frame.sequence - 1) * (60 / rate) + frame.position
    lastFrame?.let { previous -> if (current > previous + 1) lateFrames += (current - previous - 1).toInt() }
    lastFrame = current
    // The QR names the next event from half a period before its burst.
    if (frame.named != shown) {
      val image = images[frame.named] ?: return stop("Picture scheduling deadline missed. Restart the signal.")
      shown = frame.named; raster.plate = image
    }
    raster.show(frame)
    if (now - lastStatus > 0.5) {
      lastStatus = now
      // Expires when no newer clock check arrived in time (updateClock), by the rule of the app.
      var clockNote = ""
      val r = clockReference
      val anchor = r["clockMonotonicMs"]?.toDoubleOrNull(); val radius = r["clockUncertaintyMs"]?.toDoubleOrNull(); val limit = r["clockLimitMs"]?.toDoubleOrNull()
      if (usingReference && anchor != null && radius != null && limit != null) {
        val age = PhoneClock.nowMs() - anchor
        if (PhoneGenerator.clockReferenceExpired(age, radius, limit)) return stop("Clock reference expired. Close and synchronize again.")
        val adjusted = plan.clockOffsetMs(frame.sequence)
        clockNote = " (check ${(age / 1000).toLong()} s ago" +
          (if (abs(adjusted) >= 0.05) String.format(java.util.Locale.ROOT, ", %+.1f ms", adjusted) else "") + ")"
      }
      if (abs(System.currentTimeMillis() / 1000.0 - now - utcMinusHost) > 0.1) return stop("Phone clock changed. Tap Start to create a fresh clock mapping.")
      outputLatency = audio.latencyNs / 1e9
      status.text = "TEST SIGNAL ACTIVE · 1200 baud · $rate/s · frame skips $lateFrames\nEstimated output timing · ${if (usingReference) "NTP UTC$clockNote" else "device UTC"} · " +
        "audio lead ${Math.round(outputLatency * 1000)} ms$senderCalibrationNote"
    }
  }

  private fun beginPlan(target: Double) {
    sourceID = stableSourceID(context, rate)
    // UTC and host time are sampled as a bracket. Keep one mapping for the whole session; stop if the
    // wall clock later jumps instead of rewriting it.
    val before = System.nanoTime(); val utc = System.currentTimeMillis() / 1000.0; val after = System.nanoTime()
    val bootMinusHost = (SystemClock.elapsedRealtimeNanos() - System.nanoTime()) / 1e9
    epochHost = target + 1.5
    utcMinusHost = utc - (before + after) / 2e9
    var referenceUtcMinusHost = utcMinusHost
    usingReference = false
    GeneratorClock.referenceMapping(clockReference, before / 1e6 + bootMinusHost * 1000, bootMinusHost)?.let {
      referenceUtcMinusHost = it
      usingReference = true
    }
    planUtcMinusHost = referenceUtcMinusHost
    val epochTai = ((referenceUtcMinusHost + options.taiUtc + epochHost) * 1e9).roundToLong()
    val next = PhoneGenerator(sourceID, rate, epochTai, epochHost, options.name.ifEmpty { "Android" })
    plan = next
    next.setSource(options.name, options.subtitle, options.image)
    appIcon(context)?.let { next.setIcon(it) }
    applySenderCalibration(next)
    raster.foreground = next.syncForeground()
    raster.geometry = next.geometry()
    outputLatency = audio.latencyNs / 1e9
    nextAudio = 1; shown = 0; lateFrames = 0; lastFrame = null; lastStatus = 0.0; images.clear()
    generation++
    val first = next.image(1); images[1] = first
    raster.plate = first
    fillAhead(nowSeconds())
  }

  // Sound packets and pictures 2.5 s ahead. A packet must still be ahead of the output's latency.
  private fun fillAhead(now: Double) {
    val plan = plan ?: return
    while (plan.hostTime(nextAudio) < now + 2.5) {
      val host = plan.hostTime(nextAudio)
      if (host <= now + audio.latencyNs / 1e9 + 0.03) throw IllegalStateException("Scheduling deadline missed. Restart the signal.")
      audio.schedule((host * 1e9).roundToLong(), plan.audio(nextAudio))
      if (images[nextAudio] == null) {
        val sequence = nextAudio; val ticket = generation
        imageWorker.execute {
          val image = runCatching { plan.image(sequence) }.getOrNull() ?: return@execute
          raster.post { if (generation == ticket && this.plan === plan) images[sequence] = image }
        }
      }
      nextAudio++
    }
    images.keys.removeAll { it + 1 < shown }
  }

  // A newer clock check from the app (docs/phone-clock.md), like updateClock on iOS. Before the plan starts
  // (or while stopped) it replaces the reference for the next start. While the signal runs on NTP UTC, the
  // stamps of the events not scheduled yet slew to it (core retime, 100 ppm); host times and the Test ID
  // stay. A check further from the current stamps than the limit stops the signal instead.
  fun updateClock(reference: Map<String, String>) {
    if (calibrate || finishing) return
    val bootMinusHost = (SystemClock.elapsedRealtimeNanos() - System.nanoTime()) / 1e9
    val nowBootMs = PhoneClock.nowMs()
    val mapping = GeneratorClock.referenceMapping(reference, nowBootMs, bootMinusHost) ?: return
    val anchor = reference["clockMonotonicMs"]?.toDoubleOrNull() ?: return
    val radius = reference["clockUncertaintyMs"]?.toDoubleOrNull() ?: return
    val limit = reference["clockLimitMs"]?.toDoubleOrNull() ?: return
    if (PhoneGenerator.clockReferenceExpired(nowBootMs - anchor, radius, limit)) return
    val plan = plan ?: run { clockReference = reference; return }
    if (!usingReference) return // started on device UTC: stays on it until the next start
    val offset = mapping - planUtcMinusHost
    clockReference = reference
    if (plan.retime(Math.round(offset * 1e9), nextAudio, Math.round(limit * 1e6))) return
    val change = offset * 1000 - plan.clockOffsetMs(nextAudio)
    stop(String.format(java.util.Locale.ROOT, "New clock check is %+.1f ms off (more than the %.0f ms limit). Tap Start for a fresh clock mapping.", change, limit))
  }

  // ---- Sender calibration and calibration pattern ----

  // The saved screen-vs-speaker calibration goes into the AVS4 QR, but only on the audio output it was measured on.
  private fun applySenderCalibration(plan: PhoneGenerator) {
    senderCalibrationNote = ""
    val c = options.senderCalibration
    val method = c["senderCalibrationMethod"] ?: return
    val offset = c["senderCalibrationOffsetMs"]?.toDoubleOrNull() ?: return
    val uncertainty = c["senderCalibrationUncertaintyMs"]?.toDoubleOrNull() ?: return
    val day = c["senderCalibrationDay"]?.toIntOrNull() ?: return
    val route = c["senderCalibrationRoute"]
    if (route != null && route != audio.route) { senderCalibrationNote = " · sender calibration not sent (other audio output)"; return }
    plan.setSenderCalibration(method, offset, uncertainty, day)
    senderCalibrationNote = String.format(java.util.Locale.ROOT, " · sender calibration %+.1f ms", offset)
  }

  private fun startRecording() {
    if (context.checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED)
      throw IllegalStateException("The microphone is not available. Allow microphone access in Settings.")
    synchronized(recordLock) { recorded = FloatArray(MIC_RATE * (CALIBRATION_LIMIT_S + 5)); recordedCount = 0 }
    val microphone = AudioCapture(audioManager) { samples, n, _ ->
      synchronized(recordLock) {
        val take = minOf(n, recorded.size - recordedCount)
        System.arraycopy(samples, 0, recorded, recordedCount, take); recordedCount += take
      }
    }
    microphone.start(); recorder = microphone
  }

  private fun swapHost(k: Long) = epochHost + PhoneGenerator.calibrationSwapFrame(k, 60) / 60.0

  private fun calibrationTick(target: Double) {
    val now = nowSeconds()
    if (epochHost < 0) {
      if (audio.latencyNs !in 0 until 1_000_000_000L) {
        if (System.nanoTime() - startedNs > 3_000_000_000L) stop("Audio output latency is unavailable.")
        return
      }
      epochHost = target + 1.0
      nextBeep = 1; lateFrames = 0; lastFrame = null; lastAnalysis = now; calibrationResult = null; calibrationError = null
      outputLatency = audio.latencyNs / 1e9
    }
    // The beep: D = 100 ms after each swap on the same host clock (GeneratorAudio places it on the output's timestamps).
    while (swapHost(nextBeep) + 0.1 < now + 2.5) {
      val host = swapHost(nextBeep) + 0.1
      if (host <= now + audio.latencyNs / 1e9 + 0.03) return stop("Scheduling deadline missed. Tap Start to try again.")
      audio.schedule((host * 1e9).roundToLong(), BEEP)
      nextBeep++
    }
    val position = (target - epochHost) * 60
    val frame = maxOf(0L, Math.round(position))
    if (position > -0.5) {
      lastFrame?.let { previous -> if (frame > previous + 1) lateFrames += (frame - previous - 1).toInt() }
      lastFrame = frame
    }
    pattern.leftWhite = position < -0.5 || PhoneGenerator.calibrationLeftWhite(frame, 60)
    if (analysing || now - lastAnalysis < 1 || now <= epochHost + 1.5) return
    lastAnalysis = now; analysing = true
    val running = now - epochHost; val skips = lateFrames; val latency = audio.latencyNs / 1e9; val route = audio.route; val ticket = generation
    analysisWorker.execute {
      val (samples, count) = synchronized(recordLock) { recorded.copyOf(recordedCount) to recordedCount }
      val progress = JSONObject(PhoneGenerator.displayCalibration(samples, count, MIC_RATE, 1))
      val pairs = progress.optInt("pairs"); val clicks = progress.optInt("clicks")
      val done = pairs >= WANTED_PAIRS || running >= CALIBRATION_LIMIT_S
      val final = if (done) JSONObject(PhoneGenerator.displayCalibration(samples, count, MIC_RATE, 8)) else progress
      pattern.post {
        analysing = false
        if (!ticking || generation != ticket) return@post
        if (!done) {
          status.text = if (clicks == 0 && running > 3) "No click yet. Lay the calibrator on the red outline, sensors over the line, and check the cable."
            else "Measuring · $pairs of $WANTED_PAIRS swaps · frame skips $skips"
          return@post
        }
        if (final.optBoolean("ok")) {
          calibrationResult = AvsyncCameraView.toMap(final).filterValues { it != null }.mapValues { it.value!! } +
            mapOf("method" to "calibrator-display", "audioRoute" to route, "outputLatencyMs" to latency * 1000, "skippedDisplayFrames" to skips,
              "sampleRate" to MIC_RATE.toDouble())
          closeScreen()
        } else {
          calibrationError = final.optString("error").ifEmpty { null }
          stop(calibrationError ?: "Calibration failed. Tap Start to try again.")
        }
      }
    }
  }

  // ---- Views ----

  // The 1280 × 720 test raster: the event's picture, the AVS3 fields A and B, the sync square and
  // the scale's white foreground, scaled (rotated in portrait) into the view, nearest-neighbour.
  private class Raster(context: Context) : View(context) {
    var plate: Bitmap? = null
      set(value) { field = value; invalidate() }
    var foreground: Bitmap? = null
    var geometry: IntArray? = null // markerX, markerY, markerSide
    private var frame: PhoneGenerator.Frame? = null
    private val paint = Paint().apply { isFilterBitmap = false; isAntiAlias = false }
    fun show(value: PhoneGenerator.Frame) { frame = value; invalidate() }
    fun clear() { plate = null; foreground = null; frame = null; invalidate() }
    override fun onDraw(canvas: Canvas) {
      val fit = GeneratorLayout.fitRaster(width.toFloat(), height.toFloat())
      canvas.save()
      canvas.translate(width / 2f, height / 2f)
      if (fit.rotate) canvas.rotate(90f)
      canvas.scale(fit.scale, fit.scale)
      canvas.translate(-PhoneGenerator.WIDTH / 2f, -PhoneGenerator.HEIGHT / 2f)
      plate?.let { canvas.drawBitmap(it, 0f, 0f, paint) }
      geometry?.let { g ->
        // AVS3 optical code: field A (left half of the panel) and B (right half), always opposite; both black when stopped.
        val half = (g[2] / 2).toFloat(); val x = g[0].toFloat(); val y = g[1].toFloat(); val side = g[2].toFloat()
        val aWhite = frame?.aWhite == true
        paint.color = if (frame != null && aWhite) Color.WHITE else Color.BLACK
        canvas.drawRect(x, y, x + half, y + side, paint)
        paint.color = if (frame != null && !aWhite) Color.WHITE else Color.BLACK
        canvas.drawRect(x + half, y, x + side, y + side, paint)
      }
      frame?.let { f ->
        paint.color = f.syncColor
        canvas.drawRect(f.syncX, f.syncY, f.syncX + f.syncSize, f.syncY + f.syncSize, paint)
        paint.color = Color.BLACK
        val line = 2f; val cx = f.syncX + (f.syncSize - line) / 2
        canvas.drawRect(cx, f.syncY, cx + line, f.syncY + f.syncSize, paint)
      }
      foreground?.let { canvas.drawBitmap(it, 0f, 0f, paint) }
      canvas.restore()
    }
  }

  // Portrait, halves left and right (the pattern line crosses the display rows), near the microphone
  // end at the bottom. The outline is the 66 × 22 mm board, sensor end down, with a tick at the notch.
  private class CalibrationPattern(context: Context) : View(context) {
    var leftWhite = true
      set(value) { if (field != value) { field = value; invalidate() } }
    private val fill = Paint()
    private val outline = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; color = Color.rgb(255, 59, 48); strokeWidth = 2 * context.resources.displayMetrics.density }
    private val mm = GeneratorLayout.pixelsPerMm(context.resources.displayMetrics.xdpi, context.resources.displayMetrics.densityDpi.toFloat())
    override fun onDraw(canvas: Canvas) {
      val l = GeneratorLayout.calibration(width.toFloat(), height.toFloat(), mm)
      fill.color = if (leftWhite) Color.WHITE else Color.BLACK
      canvas.drawRect(0f, l.top, width / 2f, height.toFloat(), fill)
      fill.color = if (leftWhite) Color.BLACK else Color.WHITE
      canvas.drawRect(width / 2f, l.top, width.toFloat(), height.toFloat(), fill)
      canvas.drawRoundRect(l.boardLeft, l.boardTop, l.boardRight, l.boardBottom, 1.5f * mm, 1.5f * mm, outline)
      canvas.drawLine(width / 2f - 1.5f * mm, l.notchTick, width / 2f + 1.5f * mm, l.notchTick, outline)
    }
  }

  companion object {
    private const val WANTED_PAIRS = 20
    private const val CALIBRATION_LIMIT_S = 30
    private const val MIC_RATE = 48_000 // AudioCapture records at 48 kHz
    private val BEEP by lazy { PhoneGenerator.calibrationBeep(GeneratorAudio.RATE) }

    // Same install, app version (with build number) and timing settings give the same ID, so a restart keeps it.
    // The install ID is random, kept in the app's preferences and gone after a reinstall.
    // Name, subtitle and image are cosmetic and stay out. Add a new timing setting to `settings`.
    fun stableSourceID(context: Context, rate: Int): String {
      val preferences = context.getSharedPreferences("avsync", Context.MODE_PRIVATE)
      val install = preferences.getString("generator.installId", null) ?: UUID.randomUUID().toString().also {
        preferences.edit().putString("generator.installId", it).apply()
      }
      return PhoneGenerator.testId("phone-", install, appVersion(context), arrayOf("rate=$rate"))
    }

    fun appVersion(context: Context): String {
      val info = context.packageManager.getPackageInfo(context.packageName, 0)
      @Suppress("DEPRECATION") val code = if (Build.VERSION.SDK_INT >= 28) info.longVersionCode else info.versionCode.toLong()
      return "${info.versionName ?: ""} ($code)"
    }

    private fun appIcon(context: Context): Bitmap? = runCatching { context.assets.open("icon.png").use { BitmapFactory.decodeStream(it) } }.getOrNull()
  }
}

// The clock check as a mapping for the generator (JVM tests). The check is on PhoneClock (elapsedRealtime),
// the generator's host clock is System.nanoTime: UTC minus host seconds, or null for another clock or when
// the check is not 0–300 s old at nowBootMs.
object GeneratorClock {
  fun referenceMapping(r: Map<String, String>, nowBootMs: Double, bootMinusHost: Double): Double? {
    val unixMs = r["clockUnixMs"]?.toDoubleOrNull() ?: return null
    val bootMs = r["clockMonotonicMs"]?.toDoubleOrNull() ?: return null
    val age = nowBootMs - bootMs
    if (r["clockId"] != PhoneClock.identifier || unixMs !in 1e12..4e12 || !bootMs.isFinite() || !(age >= 0 && age < 300_000)) return null
    return (unixMs - bootMs) / 1000 + bootMinusHost
  }
}

// Pure layout of the generator screens (JVM tests).
object GeneratorLayout {
  class Fit(val rotate: Boolean, val scale: Float)
  // The 1280 × 720 raster in a view: rotated by 90° when the view is portrait, as large as fits.
  fun fitRaster(width: Float, height: Float): Fit {
    val portrait = height > width
    val long = if (portrait) height else width; val short = if (portrait) width else height
    return Fit(portrait, minOf(long / PhoneGenerator.WIDTH, short / PhoneGenerator.HEIGHT))
  }

  // xdpi is the physical density; a few phones report nonsense there, then the density bucket stands in.
  fun pixelsPerMm(xdpi: Float, densityDpi: Float) = (if (xdpi in 100f..1000f) xdpi else densityDpi) / 25.4f

  data class Calibration(val top: Float, val boardLeft: Float, val boardTop: Float, val boardRight: Float, val boardBottom: Float, val notchTick: Float)
  // The halves fill the bottom 62 mm (or the whole view when shorter); the board's sensors (5 mm from
  // the notch end) sit at the middle of the halves.
  fun calibration(width: Float, height: Float, mm: Float): Calibration {
    val top = maxOf(0f, height - 62 * mm)
    val notch = (top + height) / 2 + 5 * mm
    return Calibration(top, width / 2 - 11 * mm, notch - 66 * mm, width / 2 + 11 * mm, notch, notch + 2 * mm)
  }
}
