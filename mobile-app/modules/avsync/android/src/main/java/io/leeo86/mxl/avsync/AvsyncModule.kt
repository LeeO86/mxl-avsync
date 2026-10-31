package io.leeo86.mxl.avsync

import android.Manifest
import android.content.Context
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
import expo.modules.kotlin.Promise
import expo.modules.kotlin.modules.Module
import expo.modules.kotlin.modules.ModuleDefinition
import java.util.UUID

class AvsyncModule : Module() {
  private val handler = Handler(Looper.getMainLooper())
  private val clockId = UUID.randomUUID().toString()
  private var cancel: (() -> Unit)? = null
  override fun definition() = ModuleDefinition {
    Name("Avsync")
    Function("supportsLiveCapture") { false } // Camera/NDK port is a separate milestone.
    Function("supportsGenerator") { false }
    Function("deviceVersion") { Build.VERSION.RELEASE }
    Function("loadState") { appContext.reactContext?.getSharedPreferences("avsync", Context.MODE_PRIVATE)?.getString("session.v1", null) }
    Function("saveState") { value: String -> appContext.reactContext?.getSharedPreferences("avsync", Context.MODE_PRIVATE)?.edit()?.putString("session.v1", value)?.apply(); Unit }
    Function("clockNow") { mapOf("monotonicMs" to SystemClock.elapsedRealtimeNanos()/1e6, "clockId" to clockId, "timebase" to "android-elapsed-realtime") }
    AsyncFunction("synchronizeClock") { _: String, promise: Promise -> handler.post { startClock(promise) }; Unit }
    OnActivityEntersBackground { handler.post { cancel?.invoke() } }
    OnDestroy { handler.post { cancel?.invoke() } }
  }
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
            "uncertaintyMs" to v.uncertaintyMs,"clockId" to clockId,"timebase" to "android-elapsed-realtime",
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
