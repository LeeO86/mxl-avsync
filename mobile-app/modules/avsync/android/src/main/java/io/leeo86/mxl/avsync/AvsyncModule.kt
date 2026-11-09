package io.leeo86.mxl.avsync

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.location.GnssMeasurementsEvent
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.provider.MediaStore
import expo.modules.kotlin.Promise
import expo.modules.kotlin.functions.Queues
import expo.modules.kotlin.modules.Module
import expo.modules.kotlin.modules.ModuleDefinition
import android.view.WindowManager

class AvsyncModule : Module() {
  private val handler = Handler(Looper.getMainLooper())
  private var cancel: (() -> Unit)? = null
  private var ntpBusy = false
  private var generator: AvsyncGenerator? = null
  private var imagePick: Promise? = null
  private val context get() = appContext.reactContext ?: throw IllegalStateException("The app is not ready.")
  override fun definition() = ModuleDefinition {
    Name("Avsync")
    Function("supportsLiveCapture") { true }
    Function("deviceVersion") { Build.VERSION.RELEASE }
    Function("loadState") { appContext.reactContext?.getSharedPreferences("avsync", Context.MODE_PRIVATE)?.getString("session.v1", null) }
    Function("saveState") { value: String ->
      val preferences = appContext.reactContext?.getSharedPreferences("avsync", Context.MODE_PRIVATE)
      val old = imageID(preferences?.getString("session.v1", null)); val next = imageID(value)
      preferences?.edit()?.putString("session.v1", value)?.apply()
      if (old != null && old != next) GeneratorImageStore.remove(context, old)
      Unit
    }

    // The fullscreen test signal (AvsyncGenerator); the promise resolves when it closes.
    Function("supportsGenerator") { true }
    AsyncFunction("generate") { rate: Int, taiUtc: Double, promise: Promise -> presentGenerator(rate, taiUtc, emptyMap(), promise) }.runOnQueue(Queues.MAIN)
    AsyncFunction("generateStyled") { rate: Int, taiUtc: Double, options: Map<String, String>, promise: Promise ->
      presentGenerator(rate, taiUtc, options, promise)
    }.runOnQueue(Queues.MAIN)
    // A newer clock check for the running test signal (docs/phone-clock.md); ignored when none runs.
    AsyncFunction("updateGeneratorClock") { options: Map<String, String> ->
      generator?.updateClock(options.filterKeys { it.startsWith("clock") })
      Unit
    }.runOnQueue(Queues.MAIN)
    Function("generatorImageInfo") { id: String -> GeneratorImageStore.info(context, id) }
    // The system photo picker (Android 13+) or document picker: only the chosen image is shared, no storage permission.
    AsyncFunction("pickGeneratorImage") { promise: Promise ->
      val activity = appContext.currentActivity
      if (imagePick != null || activity == null || generator != null) { promise.reject("IMAGE_PICKER_BUSY", "Close the current screen first.", null); return@AsyncFunction }
      val intent = if (Build.VERSION.SDK_INT >= 33) Intent(MediaStore.ACTION_PICK_IMAGES).setType("image/*")
        else Intent(Intent.ACTION_GET_CONTENT).setType("image/*").addCategory(Intent.CATEGORY_OPENABLE)
      imagePick = promise
      try { activity.startActivityForResult(intent, PICK_IMAGE) }
      catch (e: Exception) { imagePick = null; promise.reject("IMAGE_IMPORT_FAILED", e.message ?: "No image picker on this phone.", e) }
    }.runOnQueue(Queues.MAIN)
    OnActivityResult { _, result ->
      if (result.requestCode != PICK_IMAGE) return@OnActivityResult
      val promise = imagePick ?: return@OnActivityResult
      imagePick = null
      val uri = result.data?.data
      if (result.resultCode != Activity.RESULT_OK || uri == null) { promise.resolve(null); return@OnActivityResult }
      Thread({
        runCatching { GeneratorImageStore.save(context, uri) }.fold({ promise.resolve(it) }, { promise.reject("IMAGE_IMPORT_FAILED", it.message ?: "Image import failed.", it) })
      }, "avsync.image").start()
    }
    Function("clockNow") { PhoneClock.now() }
    AsyncFunction("synchronizeClock") { server: String, promise: Promise ->
      if (ntpBusy) { promise.reject("CLOCK_BUSY", "A clock check is already running", null); return@AsyncFunction }
      ntpBusy = true
      Thread({
        val result = runCatching { SntpClient().synchronize(server) }
        handler.post {
          ntpBusy = false
          result.fold({ promise.resolve(it) }, { promise.reject("NTP_UNAVAILABLE", it.message ?: "NTP unavailable", it) })
        }
      }, "avsync.ntp").start()
    }
    // GNSS time is kept for later; the app does not use it (no location permission is requested).
    AsyncFunction("synchronizeGnssClock") { _: String, promise: Promise -> handler.post { startClock(promise) }; Unit }
    Function("setScreenAwake") { on: Boolean? ->
      handler.post {
        val window = appContext.currentActivity?.window ?: return@post
        if (on == true) window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) else window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
      }
      Unit
    }
    View(AvsyncCameraView::class) {
      Events("onReport")
      Prop("active") { view: AvsyncCameraView, active: Boolean -> view.setActive(active) }
      Prop("captureFps") { view: AvsyncCameraView, fps: Double -> view.setRequestedFps(fps) }
      Prop("taiUtcOffset") { view: AvsyncCameraView, seconds: Double -> view.setTaiUtcOffset(seconds) }
      AsyncFunction("beginCalibration") { view: AvsyncCameraView -> view.beginCalibration() }
      AsyncFunction("beginCalibrator") { view: AvsyncCameraView, mode: String -> view.beginCalibrator(mode) }
      AsyncFunction("resumeAnalysis") { view: AvsyncCameraView -> view.resumeAnalysis() }
      AsyncFunction("focusAt") { view: AvsyncCameraView, x: Double, y: Double -> view.focusAt(x, y) }
    }
    OnActivityEntersForeground { AvsyncCameraView.setForeground(true) }
    OnActivityEntersBackground { AvsyncCameraView.setForeground(false); handler.post { cancel?.invoke(); generator?.pause() } }
    OnDestroy { handler.post { cancel?.invoke(); generator?.shutdown() } }
  }

  private fun imageID(json: String?): String? = runCatching { org.json.JSONObject(json ?: return null).optString("generatorImageId").ifEmpty { null } }.getOrNull()

  private fun presentGenerator(rate: Int, taiUtc: Double, options: Map<String, String>, promise: Promise) {
    val activity = appContext.currentActivity
    if ((rate != 1 && rate != 2) || !taiUtc.isFinite() || taiUtc < 0 || taiUtc > 100 || activity == null || generator != null || imagePick != null) {
      promise.reject("GENERATOR_UNAVAILABLE", "Close the current screen and use 1 or 2 events/s.", null); return
    }
    // Whitespace runs become one space; at most `limit` characters (code points), as the app sends them.
    fun label(key: String, limit: Int): String {
      val text = (options[key] ?: "").split(Regex("\\s+")).filter { it.isNotEmpty() }.joinToString(" ")
      return text.substring(0, text.offsetByCodePoints(0, minOf(limit, text.codePointCount(0, text.length))))
    }
    try {
      val image = GeneratorImageStore.load(context, options["imageId"] ?: "")
      // mode "calibration": the calibration pattern for the calibrator board instead of the test signal.
      val screen = AvsyncGenerator(activity, AvsyncGenerator.Options(rate, taiUtc, label("name", 64), label("subtitle", 128), image,
        options.filterKeys { it.startsWith("clock") }, options["keepAwake"] != "false", options.filterKeys { it.startsWith("senderCalibration") },
        options["mode"] == "calibration")) { result -> generator = null; promise.resolve(result) }
      generator = screen
      screen.show()
    } catch (e: Exception) { generator = null; promise.reject("GENERATOR_IMAGE_UNAVAILABLE", e.message ?: "The generator could not start.", e) }
  }

  private companion object { const val PICK_IMAGE = 0x4156 }
  private fun startClock(promise: Promise) {
    val context = appContext.reactContext
    if (context == null || Build.VERSION.SDK_INT < 29) { promise.reject("GNSS_UNAVAILABLE", "GNSS clock mapping requires Android 10 or newer", null); return }
    if (cancel != null) { promise.reject("CLOCK_BUSY", "A clock check is already running", null); return }
    if (context.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
      promise.reject("GNSS_PERMISSION", "Precise location permission is required for GNSS timing", null); return
    }
    val manager = context.getSystemService(Context.LOCATION_SERVICE) as LocationManager
    if (!manager.isProviderEnabled(LocationManager.GPS_PROVIDER)) { promise.reject("GNSS_DISABLED", "Enable location and use the device outdoors with a clear sky view", null); return }
    var finished = false
    var callback: GnssMeasurementsEvent.Callback? = null
    var timeout: Runnable? = null
    var lastError = "No GNSS clock received; use a clear sky view"
    var discontinuity: Int? = null
    val samples = mutableListOf<Map<String, Any>>()
    val listener = object : LocationListener {
      override fun onLocationChanged(location: Location) {} // Neither read nor retain coordinates.
      override fun onProviderDisabled(provider: String) { cancel?.invoke() }
      override fun onProviderEnabled(provider: String) {}
      @Deprecated("Legacy API callback") override fun onStatusChanged(provider: String?, status: Int, extras: Bundle?) {}
    }
    fun finish(value: Map<String, Any>? = null, error: String? = null) {
      if (finished) return
      finished = true
      runCatching { callback?.let { manager.unregisterGnssMeasurementsCallback(it) } }
      runCatching { manager.removeUpdates(listener) }
      timeout?.let { handler.removeCallbacks(it) }
      cancel = null
      if (value != null) promise.resolve(value)
      else promise.reject("GNSS_UNAVAILABLE", error ?: lastError, null)
    }
    cancel = { finish(error = "GNSS clock check cancelled") }
    callback = object : GnssMeasurementsEvent.Callback() {
      override fun onGnssMeasurementsReceived(event: GnssMeasurementsEvent) {
        if (finished) return
        val c = event.clock
        try {
          if (discontinuity != null && discontinuity != c.hardwareClockDiscontinuityCount) samples.clear()
          discontinuity = c.hardwareClockDiscontinuityCount
          require(c.hasFullBiasNanos() && c.hasBiasUncertaintyNanos() && c.hasLeapSecond() &&
            c.hasElapsedRealtimeNanos() && c.hasElapsedRealtimeUncertaintyNanos()) { "GNSS receiver does not expose a complete clock/uncertainty mapping" }
          val now = SystemClock.elapsedRealtimeNanos()
          val v = GnssTime.convert(GnssTime.Input(c.timeNanos, c.fullBiasNanos, if(c.hasBiasNanos()) c.biasNanos else 0.0,
            c.biasUncertaintyNanos,c.elapsedRealtimeNanos,c.elapsedRealtimeUncertaintyNanos,c.leapSecond),now)
          val deviceUnix = System.currentTimeMillis().toDouble() - (now-c.elapsedRealtimeNanos)/1e6
          val result = mapOf<String, Any>("source" to "gnss", "server" to "device GNSS", "referenceUnixMs" to v.unixMs,
            "referenceMonotonicMs" to v.monotonicMs,"deviceUnixMs" to deviceUnix,"offsetMs" to v.unixMs-deviceUnix,
            "uncertaintyMs" to v.uncertaintyMs,"clockId" to PhoneClock.identifier,"timebase" to PhoneClock.TIMEBASE,
            "authenticated" to false,"hardwareDiscontinuityCount" to c.hardwareClockDiscontinuityCount,
            "uncertaintyModel" to "3-sigma GNSS bias + elapsedRealtime mapping (statistical estimate)")
          // A second independent update checks that the mapping did not jump.
          if (samples.isNotEmpty()) {
            val prior = samples.last()
            val expected = (prior["referenceUnixMs"] as Double)+v.monotonicMs-(prior["referenceMonotonicMs"] as Double)
            require(kotlin.math.abs(v.unixMs-expected) <= v.uncertaintyMs+(prior["uncertaintyMs"] as Double)+1) { "GNSS clock samples disagree" }
          }
          samples.add(result)
          if (samples.size >= 3) {
            val best = samples.minBy { it["uncertaintyMs"] as Double }
            finish(best + mapOf("samples" to samples.size,"sampleAgeMs" to (SystemClock.elapsedRealtimeNanos()/1e6-(best["referenceMonotonicMs"] as Double))))
          }
        } catch (e: Exception) { lastError = e.message ?: "Invalid GNSS clock"; samples.clear() }
      }
      override fun onStatusChanged(status: Int) {
        if (status == STATUS_NOT_SUPPORTED) finish(error="This device does not support raw GNSS clock measurements")
      }
    }
    timeout = Runnable { finish(error=lastError) }
    handler.postDelayed(timeout!!,25_000)
    try {
      if (!manager.registerGnssMeasurementsCallback(callback!!,handler)) { finish(error="GNSS measurements unavailable"); return }
      manager.requestLocationUpdates(LocationManager.GPS_PROVIDER,1000L,0f,listener,Looper.getMainLooper())
    } catch (e: Exception) { finish(error=e.message ?: "Cannot start GNSS") }
  }
}
