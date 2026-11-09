package io.leeo86.mxl.avsync

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.ImageFormat
import android.graphics.Matrix
import android.graphics.Paint
import android.graphics.Path
import android.graphics.SurfaceTexture
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraConstrainedHighSpeedCaptureSession
import android.hardware.camera2.CameraDevice
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CameraMetadata
import android.hardware.camera2.CaptureRequest
import android.hardware.camera2.CaptureResult
import android.hardware.camera2.TotalCaptureResult
import android.hardware.camera2.params.MeteringRectangle
import android.media.AudioManager
import android.media.ImageReader
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.os.SystemClock
import android.util.Range
import android.view.Surface
import android.view.TextureView
import android.view.View
import expo.modules.kotlin.AppContext
import expo.modules.kotlin.viewevent.EventDispatcher
import expo.modules.kotlin.views.ExpoView
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.Collections
import java.util.WeakHashMap
import java.util.concurrent.Executors

// The live camera view (docs/mobile-native.md). Camera2 YUV frames (or, in a constrained high-speed
// session, GPU-read luma from HighSpeedCapture) and AudioRecord blocks go to the shared PhoneAnalyzer
// on one capture thread; QR reads, live polls and calibration files run on workers.
class AvsyncCameraView(context: Context, appContext: AppContext) : ExpoView(context, appContext), TextureView.SurfaceTextureListener {
  private val onReport by EventDispatcher()
  private val preview = TextureView(context)
  private val outline = OutlineView(context)
  private val main = Handler(Looper.getMainLooper())
  private var thread = HandlerThread("avsync.capture").apply { start() }
  private var capture = Handler(thread.looper)
  private val analysis = Executors.newSingleThreadExecutor { Thread(it, "avsync.analysis") }
  private val qrWorker = Executors.newSingleThreadExecutor { Thread(it, "avsync.qr") }
  private val files = Executors.newSingleThreadExecutor { Thread(it, "avsync.files") }
  private val qr = QrReader()

  // Capture-thread state.
  private val analyzer = PhoneAnalyzer()
  private var generation = 0
  private var camera: CameraDevice? = null
  private var session: CameraCaptureSession? = null
  // The repeating request of the session, kept for tap to focus.
  private var repeating: CaptureRequest.Builder? = null
  // The 1.5 s auto step (camera start, Restart, tap to focus): frames and audio are dropped until the lock.
  private var focusing = false
  private var reader: ImageReader? = null
  private var fast: HighSpeedCapture? = null
  // Set when a high-speed session failed; the normal session is used until the rate changes.
  private var highSpeedFailed = false
  private var audio: AudioCapture? = null
  private var running = false
  private var choice: CaptureProfile.Choice? = null
  private var characteristics: CameraCharacteristics? = null
  // 'realtime' (CLOCK_BOOTTIME) or 'monotonic'; null until an UNKNOWN source is checked on the first frame.
  private var cameraClock: String? = null
  private var monotonicToBootNs = 0L
  private var exposureMode = "auto"
  private val frameMeta = object : LinkedHashMap<Long, Pair<Double, Double>>() {
    override fun removeEldestEntry(eldest: MutableMap.MutableEntry<Long, Pair<Double, Double>>?) = size > 32
  }
  @Volatile private var lastResult: TotalCaptureResult? = null
  private var analysisBusy = false
  private var qrBusy = false
  private var lastQrScan = 0L
  private var lastHousekeeping = 0L
  private var calibrationReported = false
  // Calibrator camera take: starts at the next lock (endFocus), which uses the lowest ISO (lowGain).
  private var pendingCalibrator: String? = null
  private var lowGain = false
  private var lastFrameMs = -1.0
  private var maxFrameGapMs = 0.0
  private var failedFrames = 0
  private var pollMaxMs = 0.0

  // Main-thread state.
  @Volatile private var requestedFps = 120
  @Volatile private var taiUtcSeconds = 37.0
  private var active = true
  private var surface: SurfaceTexture? = null

  init {
    preview.surfaceTextureListener = this
    addView(preview)
    addView(outline)
    views.add(this)
  }

  override fun onLayout(changed: Boolean, l: Int, t: Int, r: Int, b: Int) {
    preview.layout(0, 0, r - l, b - t)
    outline.layout(0, 0, r - l, b - t)
    configureTransform()
  }

  fun setActive(value: Boolean) { active = value; updateRunning() }
  fun setTaiUtcOffset(seconds: Double) { taiUtcSeconds = seconds }
  fun setRequestedFps(fps: Double) {
    val value = fps.toInt()
    if (value !in listOf(0, 30, 60, 120, 240) || value == requestedFps) return
    requestedFps = value
    capture.post {
      highSpeedFailed = false
      if (!running) return@post
      stopCapture(); startCapture()
      emit(mapOf("kind" to "status", "fps" to 0, "message" to "Capture rate changed · collecting fresh events"))
    }
  }
  fun beginCalibration() { outline.clear(); capture.post { generation++; focusing = false; calibrationReported = false; analyzer.beginCalibration() } }
  // Calibrator board take ("camera" or "display", docs/calibrator.md). For the camera the LED floods
  // the lens: run the auto step again and lock at the lowest ISO; the take starts at the lock.
  fun beginCalibrator(mode: String) {
    outline.clear()
    capture.post {
      generation++; focusing = false; calibrationReported = false; analyzer.reset()
      if (mode != "camera") { pendingCalibrator = null; lowGain = false; analyzer.beginCalibrator(mode); return@post }
      pendingCalibrator = mode; lowGain = true
      refocus(null, null)
    }
  }
  // Restart: drop the history and run the auto step again, metered at the centre (also cancels a calibrator take).
  fun resumeAnalysis() { outline.clear(); capture.post { generation++; focusing = false; calibrationReported = false; pendingCalibrator = null; lowGain = false; analyzer.reset(); refocus(null, null) } }
  fun focusAt(x: Double, y: Double) { capture.post { if (pendingCalibrator == null && !analyzer.calibratorActive()) refocus(x, y) } }

  override fun onAttachedToWindow() {
    super.onAttachedToWindow()
    if (!thread.isAlive) { thread = HandlerThread("avsync.capture").apply { start() }; capture = Handler(thread.looper) }
    updateRunning()
  }
  override fun onDetachedFromWindow() {
    super.onDetachedFromWindow()
    capture.post { stopCapture() }
    thread.quitSafely()
  }

  override fun onSurfaceTextureAvailable(texture: SurfaceTexture, width: Int, height: Int) { surface = texture; updateRunning() }
  override fun onSurfaceTextureSizeChanged(texture: SurfaceTexture, width: Int, height: Int) = configureTransform()
  override fun onSurfaceTextureDestroyed(texture: SurfaceTexture): Boolean {
    surface = null
    // The camera must stop writing into the texture before it is released.
    val stopped = java.util.concurrent.CountDownLatch(1)
    capture.post { stopCapture(); stopped.countDown() }
    stopped.await(1, java.util.concurrent.TimeUnit.SECONDS)
    return true
  }
  override fun onSurfaceTextureUpdated(texture: SurfaceTexture) {}

  private fun updateRunning() {
    val run = isAttachedToWindow && active && appForeground && surface != null
    capture.post { if (run && !running) startCapture() else if (!run && running) stopCapture() }
  }

  // ---- Capture thread ----

  @SuppressLint("MissingPermission")
  private fun startCapture() {
    if (context.checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED ||
        context.checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
      emit(mapOf("kind" to "error", "message" to "Allow camera and microphone access.")); return
    }
    try {
      val manager = context.getSystemService(Context.CAMERA_SERVICE) as CameraManager
      val id = manager.cameraIdList.firstOrNull { manager.getCameraCharacteristics(it).get(CameraCharacteristics.LENS_FACING) == CameraMetadata.LENS_FACING_BACK }
        ?: throw IllegalStateException("Camera or microphone unavailable on this device.")
      val c = manager.getCameraCharacteristics(id)
      val map = c.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)!!
      // The preview stream runs at the same rate, so the slower of the two limits the size.
      val outputs = map.getOutputSizes(ImageFormat.YUV_420_888).map {
        CaptureProfile.Output(CaptureProfile.Size(it.width, it.height), maxOf(map.getOutputMinFrameDuration(ImageFormat.YUV_420_888, it),
          runCatching { map.getOutputMinFrameDuration(SurfaceTexture::class.java, it) }.getOrDefault(0L)))
      }
      val ranges = c.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES).orEmpty().map { it.lower to it.upper }
      val normal = CaptureProfile.choose(ranges, outputs, requestedFps)
      val highSpeed = if (highSpeedFailed || CameraMetadata.REQUEST_AVAILABLE_CAPABILITIES_CONSTRAINED_HIGH_SPEED_VIDEO !in
          c.get(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES)?.toList().orEmpty()) null
        else CaptureProfile.chooseHighSpeed(map.highSpeedVideoSizes.orEmpty().map { size ->
          CaptureProfile.HighSpeed(CaptureProfile.Size(size.width, size.height), map.getHighSpeedVideoFpsRangesFor(size).map { it.lower to it.upper })
        }, requestedFps, normal?.fps ?: 0)
      val picked = highSpeed ?: normal ?: throw IllegalStateException("Requested camera rate is unsupported. Choose another rate.")
      characteristics = c; choice = picked; lastResult = null
      // REALTIME timestamps are CLOCK_BOOTTIME, like the microphone. UNKNOWN is checked on the first frame.
      cameraClock = if (c.get(CameraCharacteristics.SENSOR_INFO_TIMESTAMP_SOURCE) == CameraMetadata.SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME) "realtime" else null
      monotonicToBootNs = SystemClock.elapsedRealtimeNanos() - System.nanoTime()
      generation++; focusing = false; calibrationReported = false; analyzer.reset(); lastFrameMs = -1.0; maxFrameGapMs = 0.0; failedFrames = 0
      main.post { configureTransform() }
      if (!picked.highSpeed) {
        val imageReader = ImageReader.newInstance(picked.size.width, picked.size.height, ImageFormat.YUV_420_888, 4)
        imageReader.setOnImageAvailableListener({ onFrame(it) }, capture)
        reader = imageReader
      }
      val microphone = AudioCapture(context.getSystemService(Context.AUDIO_SERVICE) as AudioManager) { samples, n, pts ->
        capture.post { if (running && !focusing) { analyzer.pushAudio(samples, n, pts, 48_000); housekeeping() } }
      }
      running = true
      manager.openCamera(id, object : CameraDevice.StateCallback() {
        override fun onOpened(device: CameraDevice) { if (!running) { device.close(); return }; camera = device; openSession(device) }
        override fun onDisconnected(device: CameraDevice) { device.close(); if (camera == device) fail("Camera disconnected.") }
        override fun onError(device: CameraDevice, error: Int) { device.close(); if (camera == device || camera == null) fail("Camera error $error.") }
      }, capture)
      microphone.start(); audio = microphone
      emit(mapOf("kind" to "status", "fps" to 0, "message" to "Camera ready. Live analysis starts as frames and audio arrive."))
    } catch (e: Exception) { fail(e.message ?: "Cannot start the camera.") }
  }

  private fun fail(message: String) { stopCapture(); emit(mapOf("kind" to "error", "message" to message)) }

  private fun stopCapture() {
    generation++
    running = false
    focusing = false
    repeating = null
    audio?.stop(); audio = null
    runCatching { session?.close() }; session = null
    runCatching { camera?.close() }; camera = null
    runCatching { reader?.close() }; reader = null
    runCatching { fast?.close() }; fast = null
    capture.removeCallbacksAndMessages(LOCK)
    main.post { outline.clear() }
  }

  @Suppress("DEPRECATION") // createCaptureSession(List<Surface>, …): the only form on API 24–27.
  private fun openSession(device: CameraDevice) {
    val texture = surface ?: return fail("Camera preview unavailable.")
    val picked = choice!!
    if (picked.highSpeed) return openHighSpeedSession(device, texture, picked)
    texture.setDefaultBufferSize(picked.size.width, picked.size.height)
    val targets = listOf(Surface(texture), reader!!.surface)
    device.createCaptureSession(targets, object : CameraCaptureSession.StateCallback() {
      override fun onConfigured(s: CameraCaptureSession) {
        if (camera != device) { s.close(); return }
        session = s
        val request = device.createCaptureRequest(CameraDevice.TEMPLATE_RECORD).apply {
          targets.forEach { addTarget(it) }
          set(CaptureRequest.CONTROL_MODE, CaptureRequest.CONTROL_MODE_AUTO)
          set(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, Range(picked.aeMin, picked.aeMax))
          set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_VIDEO)
          set(CaptureRequest.CONTROL_VIDEO_STABILIZATION_MODE, CaptureRequest.CONTROL_VIDEO_STABILIZATION_MODE_OFF)
        }
        exposureMode = "auto"
        repeating = request; focusing = true
        s.setRepeatingRequest(request.build(), results, capture)
        // About 1.5 s of automatic exposure, then fixed focus, white balance and exposure.
        capture.postAtTime({ lockExposure(device, s, request) }, LOCK, SystemClock.uptimeMillis() + 1500)
      }
      override fun onConfigureFailed(s: CameraCaptureSession) = fail("Cannot configure the camera for ${picked.size.width}×${picked.size.height} at ${picked.fps} fps.")
    }, capture)
  }

  // One output (the GPU texture): with two, the burst list would give the second only 30 fps.
  // HighSpeedCapture draws the preview from the same frames.
  @Suppress("DEPRECATION") // the List<Surface> form: the only one on API 24–27.
  private fun openHighSpeedSession(device: CameraDevice, texture: SurfaceTexture, picked: CaptureProfile.Choice) {
    val sideways = sensorRotation() % 180 != 0
    val gpu = try {
      HighSpeedCapture(picked.size.width, picked.size.height, texture,
        if (sideways) picked.size.height else picked.size.width, if (sideways) picked.size.width else picked.size.height,
        maxOf(1, picked.fps / 30), capture, { onHighSpeedFrame(it) }, { fallBack(it) })
    } catch (e: Exception) { return fallBack(e.message ?: "GPU setup failed") }
    fast = gpu
    device.createConstrainedHighSpeedCaptureSession(listOf(gpu.surface), object : CameraCaptureSession.StateCallback() {
      override fun onConfigured(s: CameraCaptureSession) {
        if (camera != device) { s.close(); return }
        session = s
        val burst = s as CameraConstrainedHighSpeedCaptureSession
        val request = device.createCaptureRequest(CameraDevice.TEMPLATE_RECORD).apply {
          addTarget(gpu.surface)
          set(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, Range(picked.aeMin, picked.aeMax))
        }
        exposureMode = "auto"
        repeating = request; focusing = true
        try { burst.setRepeatingBurst(burst.createHighSpeedRequestList(request.build()), results, capture) }
        catch (e: Exception) { return fallBack(e.message ?: "burst refused") }
        capture.postAtTime({ lockExposure(device, s, request) }, LOCK, SystemClock.uptimeMillis() + 1500)
        capture.postAtTime({ if (session == s && lastFrameMs < 0) fallBack("no frames") }, LOCK, SystemClock.uptimeMillis() + 3000)
      }
      override fun onConfigureFailed(s: CameraCaptureSession) = fallBack("session not configured")
    }, capture)
  }

  // High-speed capture did not work: restart with the normal session until the rate changes.
  private fun fallBack(reason: String) {
    if (!running) return
    highSpeedFailed = true
    stopCapture(); startCapture()
    emit(mapOf("kind" to "status", "fps" to 0, "message" to "High-speed capture failed ($reason) · using normal capture"))
  }

  // Exposure = the full frame interval, with the ISO that keeps the automatic brightness.
  private fun lockExposure(device: CameraDevice, s: CameraCaptureSession, request: CaptureRequest.Builder, tries: Int = 0) {
    if (session != s) return
    // The first result can come late (slow cameras, the emulator): try again every 100 ms for 3 s,
    // then give up on the lock and end the hold, so the analysis runs with what the camera has.
    val result = lastResult ?: return if (tries < 30) retryLock(device, s, request, tries + 1) else endFocus()
    if (s is CameraConstrainedHighSpeedCaptureSession) return lockHighSpeed(s, request)
    val c = characteristics!!
    val capabilities = c.get(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES)?.toList().orEmpty()
    val frameNs = 1_000_000_000L / choice!!.fps
    request.set(CaptureRequest.CONTROL_AWB_LOCK, true)
    result.get(CaptureResult.LENS_FOCUS_DISTANCE)?.let { distance ->
      if (CameraMetadata.REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR in capabilities) {
        request.set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_OFF)
        request.set(CaptureRequest.LENS_FOCUS_DISTANCE, distance)
      }
    }
    val exposure = result.get(CaptureResult.SENSOR_EXPOSURE_TIME)
    val iso = result.get(CaptureResult.SENSOR_SENSITIVITY)
    val exposureRange = c.get(CameraCharacteristics.SENSOR_INFO_EXPOSURE_TIME_RANGE)
    val isoRange = c.get(CameraCharacteristics.SENSOR_INFO_SENSITIVITY_RANGE)
    if (CameraMetadata.REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR in capabilities && exposure != null && iso != null && exposureRange != null && isoRange != null) {
      val (fixed, auto) = CaptureProfile.fixedExposure(exposure, iso, frameNs, exposureRange.lower..exposureRange.upper, isoRange.lower..isoRange.upper)
      val gain = if (lowGain) isoRange.lower else auto
      request.set(CaptureRequest.CONTROL_AE_MODE, CaptureRequest.CONTROL_AE_MODE_OFF)
      request.set(CaptureRequest.SENSOR_EXPOSURE_TIME, fixed)
      request.set(CaptureRequest.SENSOR_SENSITIVITY, gain)
      request.set(CaptureRequest.SENSOR_FRAME_DURATION, frameNs)
      exposureMode = if (fixed >= frameNs * 95 / 100) "fixed-frame-interval" else "fixed-short"
    } else {
      request.set(CaptureRequest.CONTROL_AE_LOCK, true)
      exposureMode = "ae-locked"
    }
    runCatching { s.setRepeatingRequest(request.build(), results, capture) }.onFailure { fail(it.message ?: "Cannot lock the camera exposure.") }
    endFocus()
  }

  private fun retryLock(device: CameraDevice, s: CameraCaptureSession, request: CaptureRequest.Builder, tries: Int) {
    capture.postAtTime({ lockExposure(device, s, request, tries) }, LOCK, SystemClock.uptimeMillis() + 100)
  }

  // High-speed requests ignore manual exposure and focus: lock AE and AWB where AE stands, and
  // lock focus with one AF trigger (the burst keeps continuous-video AF).
  private fun lockHighSpeed(s: CameraConstrainedHighSpeedCaptureSession, request: CaptureRequest.Builder) {
    request.set(CaptureRequest.CONTROL_AE_LOCK, true)
    request.set(CaptureRequest.CONTROL_AWB_LOCK, true)
    runCatching {
      request.set(CaptureRequest.CONTROL_AF_TRIGGER, CaptureRequest.CONTROL_AF_TRIGGER_START)
      s.captureBurst(s.createHighSpeedRequestList(request.build()), results, capture)
      request.set(CaptureRequest.CONTROL_AF_TRIGGER, CaptureRequest.CONTROL_AF_TRIGGER_IDLE)
      s.setRepeatingBurst(s.createHighSpeedRequestList(request.build()), results, capture)
      exposureMode = "ae-locked"
    }.onFailure { fail(it.message ?: "Cannot lock the camera exposure.") }
    endFocus()
  }

  // Tap to focus (x, y: 0…1 across the view) or Restart (null: the camera's default, centred
  // metering): hold the analysis, re-run the 1.5 s auto step with AF and AE metered there, lock
  // again as at start, then start fresh (endFocus), so no frame from the auto step reaches a reading.
  private fun refocus(x: Double?, y: Double?) {
    val s = session ?: return; val device = camera ?: return; val request = repeating ?: return
    val c = characteristics ?: return; val picked = choice ?: return
    if (!running || analyzer.calibrating() || lastFrameMs < 0) return
    val region = if (x == null || y == null) null else {
      val active = c.get(CameraCharacteristics.SENSOR_INFO_ACTIVE_ARRAY_SIZE) ?: return
      val (x0, y0, side) = CaptureProfile.meteringSquare(x, y, width, height, sensorRotation(), picked.size, active.width(), active.height())
      arrayOf(MeteringRectangle(x0, y0, side, side, MeteringRectangle.METERING_WEIGHT_MAX))
    }
    capture.removeCallbacksAndMessages(LOCK)
    generation++; focusing = true; exposureMode = "auto"
    main.post { outline.clear() }
    if ((c.get(CameraCharacteristics.CONTROL_MAX_REGIONS_AF) ?: 0) > 0) request.set(CaptureRequest.CONTROL_AF_REGIONS, region)
    if ((c.get(CameraCharacteristics.CONTROL_MAX_REGIONS_AE) ?: 0) > 0) request.set(CaptureRequest.CONTROL_AE_REGIONS, region)
    request.set(CaptureRequest.CONTROL_AE_LOCK, false)
    request.set(CaptureRequest.CONTROL_AWB_LOCK, false)
    // Cameras without manual exposure: the calibrator take meters as dark as allowed instead of the lowest ISO.
    request.set(CaptureRequest.CONTROL_AE_EXPOSURE_COMPENSATION, if (lowGain) c.get(CameraCharacteristics.CONTROL_AE_COMPENSATION_RANGE)?.lower ?: 0 else 0)
    runCatching {
      if (s is CameraConstrainedHighSpeedCaptureSession) {
        // AF stays continuous-video in this mode: one cancel trigger releases the locked focus.
        request.set(CaptureRequest.CONTROL_AF_TRIGGER, CaptureRequest.CONTROL_AF_TRIGGER_CANCEL)
        s.captureBurst(s.createHighSpeedRequestList(request.build()), results, capture)
        request.set(CaptureRequest.CONTROL_AF_TRIGGER, CaptureRequest.CONTROL_AF_TRIGGER_IDLE)
        s.setRepeatingBurst(s.createHighSpeedRequestList(request.build()), results, capture)
      } else {
        request.set(CaptureRequest.CONTROL_AE_MODE, CaptureRequest.CONTROL_AE_MODE_ON)
        request.set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_VIDEO)
        s.setRepeatingRequest(request.build(), results, capture)
      }
    }.onFailure { return fail(it.message ?: "Cannot refocus the camera.") }
    capture.postAtTime({ lockExposure(device, s, request) }, LOCK, SystemClock.uptimeMillis() + 1500)
  }

  // After the lock: drop what came before and start fresh.
  private fun endFocus() {
    if (!focusing) return
    focusing = false; generation++; calibrationReported = false; analyzer.reset()
    pendingCalibrator?.let { analyzer.beginCalibrator(it) }
    pendingCalibrator = null; lowGain = false
  }

  private val results = object : CameraCaptureSession.CaptureCallback() {
    override fun onCaptureCompleted(s: CameraCaptureSession, request: CaptureRequest, result: TotalCaptureResult) {
      lastResult = result
      val timestamp = result.get(CaptureResult.SENSOR_TIMESTAMP) ?: return
      val exposure = (result.get(CaptureResult.SENSOR_EXPOSURE_TIME) ?: 0L) / 1e6
      val readout = (result.get(CaptureResult.SENSOR_ROLLING_SHUTTER_SKEW) ?: 0L) / 1e6
      frameMeta[timestamp] = exposure to readout
    }
    override fun onCaptureFailed(s: CameraCaptureSession, request: CaptureRequest, failure: android.hardware.camera2.CaptureFailure) { failedFrames++ }
  }

  private fun bootMs(timestampNs: Long) = (if (cameraClock == "realtime") timestampNs else timestampNs + monotonicToBootNs) / 1e6

  // An UNKNOWN source: the frame started about one frame ago on whichever clock matches within 100 ms.
  private fun identifyClock(timestampNs: Long): Boolean {
    val frameNs = 1_000_000_000L / (choice?.fps ?: 30)
    val boot = SystemClock.elapsedRealtimeNanos(); val monotonic = System.nanoTime()
    cameraClock = when {
      kotlin.math.abs(boot - frameNs - timestampNs) < 100_000_000L -> "realtime"
      kotlin.math.abs(monotonic - frameNs - timestampNs) < 100_000_000L -> "monotonic"
      else -> null
    }
    if (cameraClock == null) fail("Camera clock unknown on this device")
    return cameraClock != null
  }

  private fun onFrame(source: ImageReader) {
    val image = runCatching { source.acquireNextImage() }.getOrNull() ?: return
    image.use {
      val plane = it.planes[0]
      pushFrame(it.timestamp, plane.buffer, it.width, it.height, plane.rowStride) { qr.copy(plane.buffer, it.width, it.height, plane.rowStride) }
    }
  }

  // The flash detector only samples 50 points, so live frames are read 320 px wide; a calibration
  // take keeps up to 640 px (the core's review copy), and QR reads get 960 px (QrReader's limit).
  private fun onHighSpeedFrame(timestampNs: Long) {
    val gpu = fast ?: return
    val luma = gpu.read(if (analyzer.calibrating()) 640 else 320)
    pushFrame(timestampNs, luma.buffer, luma.width, luma.height, luma.width) { gpu.read(960).let { qr.copy(it.buffer, it.width, it.height, it.width) } }
  }

  // One frame from either path: timestamp on the camera clock, luma plane, and a QR copy on demand.
  private fun pushFrame(timestampNs: Long, luma: java.nio.ByteBuffer, width: Int, height: Int, stride: Int, qrCopy: () -> QrReader.Frame) {
    if (!running) return
    if (cameraClock == null && !identifyClock(timestampNs)) return
    val pts = bootMs(timestampNs)
    // The result can arrive after the image; then the previous frame's values stand in.
    val (exposure, readout) = frameMeta[timestampNs] ?: frameMeta.values.lastOrNull() ?: (0.0 to 0.0)
    if (lastFrameMs >= 0) maxFrameGapMs = maxOf(maxFrameGapMs, pts - lastFrameMs)
    lastFrameMs = pts
    // Counted (the high-speed no-frames check needs it) but not analysed during the auto step.
    if (focusing) return
    analyzer.pushVideo(luma, width, height, stride, pts, exposure, readout)
    val now = SystemClock.uptimeMillis()
    if (!qrBusy && now - lastQrScan >= 100 && analyzer.qrScanSuggested(pts)) {
      qrBusy = true; lastQrScan = now
      val frame = qrCopy()
      val scanGeneration = generation
      qrWorker.execute {
        val found = runCatching { qr.read(frame) }.getOrDefault(emptyList())
        capture.post {
          qrBusy = false
          if (scanGeneration == generation) found.forEach { d -> analyzer.observeQr(d.payload, d.corners, pts) }
        }
      }
    }
    housekeeping()
  }

  // Every 100 ms: device-UTC offset, preview outline, and the calibration or live poll.
  private fun housekeeping() {
    val now = SystemClock.uptimeMillis()
    if (now - lastHousekeeping < 100) return
    lastHousekeeping = now
    val before = SystemClock.elapsedRealtimeNanos(); val unix = System.currentTimeMillis(); val after = SystemClock.elapsedRealtimeNanos()
    analyzer.setDeviceUtcOffset(unix - (before + after) / 2e6)
    if (cameraClock == "monotonic") monotonicToBootNs = SystemClock.elapsedRealtimeNanos() - System.nanoTime()
    analyzer.setTaiUtcSeconds(taiUtcSeconds)
    val corners = analyzer.picture()
    main.post { outline.show(corners) }
    // A calibrator take reports about once a second (as iOS: otherwise the live poll goes on).
    analyzer.pollCalibrator()?.let { text ->
      val report = toMap(JSONObject(text))
      if (report["kind"] != "calibrator-progress") analyzer.reset() // done: live analysis resumes
      emit(report)
      return
    }
    if (analyzer.calibrating()) {
      if (calibrationReported) return
      analyzer.takeCalibration()?.let { take ->
        calibrationReported = true
        val directory = File(context.cacheDir, "avsync-calibration/${System.currentTimeMillis()}")
        files.execute {
          val report = runCatching { CalibrationFiles.write(take, directory) }
          emit(report.getOrElse { e -> mapOf("kind" to "error", "message" to (e.message ?: "Could not save the calibration take.")) })
        }
        return
      }
      analyzer.calibrationError()?.let { message -> calibrationReported = true; emit(mapOf("kind" to "error", "message" to message)) }
      return
    }
    if (analysisBusy || !analyzer.reportDue()) return
    analysisBusy = true
    val snapshot = analyzer.snapshotForPoll()
    val pollGeneration = generation
    analysis.execute {
      val started = SystemClock.elapsedRealtimeNanos()
      val text = snapshot.poll()
      val elapsed = (SystemClock.elapsedRealtimeNanos() - started) / 1e6
      capture.post {
        analysisBusy = false
        if (pollGeneration == generation && text != null) {
          analyzer.mergeAudio(snapshot)
          pollMaxMs = maxOf(pollMaxMs, elapsed)
          val stats = mapOf("maxFrameGapMs" to maxFrameGapMs, "dropCallbacks" to failedFrames, "pollProcessingMaxMs" to pollMaxMs)
          // High-speed path: the longest GPU draw + read of one frame since the last report.
          val gpuRead = fast?.let { mapOf("gpuReadMaxMs" to it.takeReadMaxMs()) }.orEmpty()
          emit(toMap(JSONObject(text)) + mapOf("analysisMode" to "async-snapshot-v1", "captureStats" to stats + gpuRead))
          maxFrameGapMs = 0.0
        }
        snapshot.close()
      }
    }
  }

  private fun captureContext(): Map<String, Any> {
    val picked = choice
    return mapOf("platform" to "android", "model" to "${Build.MANUFACTURER} ${Build.MODEL}", "os" to Build.VERSION.RELEASE,
      "audioRoute" to (audio?.route ?: "unknown"), "sampleRate" to 48_000.0,
      "cameraMode" to (picked?.let { "${it.size.width}x${it.size.height}@${it.fps}" } ?: "unknown"),
      "capturePath" to (picked?.let { if (it.highSpeed) "high_speed" else "normal" } ?: "unknown"),
      "exposureMode" to exposureMode, "cameraClock" to (cameraClock ?: "unknown"),
      "audioClock" to (audio?.clock ?: "unknown"))
  }

  private fun emit(value: Map<String, Any?>) {
    val report = value.toMutableMap()
    // Exposure once locked (as iOS reports it; 0 = still automatic): what the last frame used.
    report["exposureMs"] = if (exposureMode == "auto") 0.0 else (lastResult?.get(CaptureResult.SENSOR_EXPOSURE_TIME) ?: 0L) / 1e6
    report["configuredFps"] = choice?.fps?.toDouble() ?: 0.0
    report["requestedFps"] = requestedFps.toDouble()
    report["captureContext"] = captureContext()
    val time = value["timeMs"] as? Double; val offset = value["deviceUtcOffsetMs"] as? Double
    // Capture time is elapsedRealtime, the same clock as clockNow(); the mapping needs no conversion.
    if (time != null && offset != null)
      report["clockMapping"] = mapOf("monotonicMs" to time, "deviceUnixMs" to time + offset, "clockId" to PhoneClock.identifier, "timebase" to PhoneClock.TIMEBASE)
    @Suppress("UNCHECKED_CAST") val event = report.filterValues { it != null } as Map<String, Any>
    main.post { onReport(event) }
  }

  // ---- Main thread: preview geometry ----

  private fun sensorRotation() = characteristics?.get(CameraCharacteristics.SENSOR_ORIENTATION) ?: 90

  // Aspect-fill: TextureView stretches the (rotated) camera picture to the view; undo that and crop.
  private fun configureTransform() {
    val picked = choice ?: return
    val w = width.toFloat(); val h = height.toFloat()
    if (w <= 0 || h <= 0) return
    val sideways = sensorRotation() % 180 != 0
    val cw = (if (sideways) picked.size.height else picked.size.width).toFloat()
    val ch = (if (sideways) picked.size.width else picked.size.height).toFloat()
    val scale = maxOf(w / cw, h / ch)
    preview.setTransform(Matrix().apply { setScale(cw * scale / w, ch * scale / h, w / 2, h / 2) })
    outline.geometry(sensorRotation(), cw * scale, ch * scale)
  }

  // The detected picture outline, mapped from camera-image corners to the cropped preview.
  private class OutlineView(context: Context) : View(context) {
    private val paint = Paint().apply { color = Color.rgb(52, 199, 89); style = Paint.Style.STROKE; strokeWidth = 6f; strokeJoin = Paint.Join.ROUND; isAntiAlias = true }
    private var corners: DoubleArray? = null
    private var seen = 0L
    private var rotation = 90
    private var shownW = 0f; private var shownH = 0f
    fun geometry(rotation: Int, shownWidth: Float, shownHeight: Float) { this.rotation = rotation; shownW = shownWidth; shownH = shownHeight; invalidate() }
    fun show(value: DoubleArray?) {
      val now = SystemClock.uptimeMillis()
      if (value != null) { corners = value; seen = now } else if (now - seen > 500) corners = null
      invalidate()
    }
    fun clear() { corners = null; invalidate() }
    override fun onDraw(canvas: Canvas) {
      val c = corners ?: return
      if (shownW <= 0) return
      val path = Path()
      for (i in 0 until 4) {
        val u = c[i]; val v = c[i + 4]
        val (p, q) = when (rotation) { 90 -> 1 - v to u; 180 -> 1 - u to 1 - v; 270 -> v to 1 - u; else -> u to v }
        val x = (width / 2 + (p - .5) * shownW).toFloat(); val y = (height / 2 + (q - .5) * shownH).toFloat()
        if (i == 0) path.moveTo(x, y) else path.lineTo(x, y)
      }
      path.close()
      canvas.drawPath(path, paint)
    }
  }

  companion object {
    private val LOCK = Any()
    // Module lifecycle events reach every live view.
    private val views: MutableSet<AvsyncCameraView> = Collections.newSetFromMap(WeakHashMap())
    @Volatile private var appForeground = true
    fun setForeground(value: Boolean) { appForeground = value; views.toList().forEach { it.updateRunning() } }

    fun toMap(json: JSONObject): Map<String, Any?> = json.keys().asSequence().associateWith { value(json.get(it)) }
    private fun value(v: Any?): Any? = when (v) {
      is JSONObject -> toMap(v)
      is JSONArray -> (0 until v.length()).map { value(v.get(it)) }
      JSONObject.NULL -> null
      else -> v
    }
  }
}
