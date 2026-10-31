package io.leeo86.mxl.avsync

// Pure conversion, shared by the real callback and JVM tests. GNSS elapsedRealtime ties the
// receiver's clock to Android CLOCK_BOOTTIME; callback arrival time is never the GNSS timestamp.
object GnssTime {
  data class Input(val timeNanos: Long, val fullBiasNanos: Long, val biasNanos: Double,
    val biasUncertaintyNanos: Double, val elapsedRealtimeNanos: Long,
    val elapsedUncertaintyNanos: Double, val leapSeconds: Int)
  data class Reference(val unixMs: Double, val monotonicMs: Double, val uncertaintyMs: Double)
  fun convert(v: Input, nowNanos: Long): Reference {
    require(v.biasNanos.isFinite() && v.biasUncertaintyNanos.isFinite() && v.biasUncertaintyNanos >= 0 &&
      v.elapsedUncertaintyNanos.isFinite() && v.elapsedUncertaintyNanos >= 0) { "GNSS clock uncertainty unavailable" }
    require(v.leapSeconds in 0..50) { "GNSS leap-second offset invalid" }
    require(v.elapsedRealtimeNanos > 0 && v.elapsedRealtimeNanos <= nowNanos &&
      nowNanos - v.elapsedRealtimeNanos <= 1_000_000_000L) { "GNSS clock sample is stale" }
    // Subtract integer clocks before converting to Double: avoids loss of low bits at epoch scale.
    val gpsNanos = Math.subtractExact(v.timeNanos, v.fullBiasNanos)
    val utcMs = (gpsNanos.toDouble() - v.biasNanos) / 1e6 + 315_964_800_000.0 - v.leapSeconds * 1000.0
    require(utcMs in 1e12..4e12) { "GNSS calendar is implausible" }
    // Android reports 1-sigma uncertainties. x3 is a statistical estimate, not a hard error bound.
    val radius = 3 * (v.biasUncertaintyNanos + v.elapsedUncertaintyNanos) / 1e6 + 0.001
    require(radius.isFinite() && radius <= 1500) { "GNSS clock uncertainty excessive" }
    return Reference(utcMs, v.elapsedRealtimeNanos / 1e6, radius)
  }
}
