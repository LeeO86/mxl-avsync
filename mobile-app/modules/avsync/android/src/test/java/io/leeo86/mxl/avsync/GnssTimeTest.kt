package io.leeo86.mxl.avsync
import org.junit.Test
import org.junit.Assert.*
class GnssTimeTest {
  private val epoch=1_793_450_000_000L
  private val gpsNs=(epoch-315_964_800_000L+18_000L)*1_000_000
  private val v=GnssTime.Input(10_000_000_000L,10_000_000_000L-gpsNs,0.0,20.0,10_000_000_000L,30.0,18)
  @Test fun mapsReceiverTimeToElapsedRealtimeAndUtc() {
    val r=GnssTime.convert(v,10_100_000_000L)
    assertEquals(epoch.toDouble(),r.unixMs,.001)
    assertEquals(10000.0,r.monotonicMs,.001)
    assertTrue(r.uncertaintyMs>=.00115)
  }
  @Test fun rejectsInvalidOrStaleReferences() {
    for (bad in listOf(v.copy(biasUncertaintyNanos=Double.NaN),v.copy(leapSeconds=100),v.copy(elapsedRealtimeNanos=20_000_000_000L),v.copy(elapsedRealtimeNanos=1L),v.copy(elapsedUncertaintyNanos=-1.0))) {
      try { GnssTime.convert(bad,10_100_000_000L);fail("Accepted invalid GNSS sample") } catch(_:IllegalArgumentException) {}
    }
  }
}
