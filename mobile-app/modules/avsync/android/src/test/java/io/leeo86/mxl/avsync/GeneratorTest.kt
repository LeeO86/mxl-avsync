package io.leeo86.mxl.avsync
import org.junit.Test
import org.junit.Assert.*
class GeneratorTest {
  @Test fun packetStartsOnTheOutputTimestamp() {
    // Frame 48 000 plays at 10 s: a packet planned for 10.5 s starts 24 000 frames later.
    assertEquals(72_000L, GeneratorAudio.startFrame(10_500_000_000L, 48_000, 10_000_000_000L))
    // Rounded to the nearest frame (one frame = 20.83 µs).
    assertEquals(48_001L, GeneratorAudio.startFrame(10_000_015_000L, 48_000, 10_000_000_000L))
    assertEquals(48_000L, GeneratorAudio.startFrame(10_000_010_000L, 48_000, 10_000_000_000L))
    assertEquals(5_000_000L, GeneratorAudio.framesToNs(240))
  }
  @Test fun mixPlacesSamplesExactly() {
    val packet = FloatArray(300) { it + 1f }
    // The packet starts 100 frames into the block at position 1000 and runs into the next block.
    val first = FloatArray(GeneratorAudio.BLOCK)
    assertTrue(GeneratorAudio.mix(first, 1000, packet, 1100))
    assertEquals(0f, first[99]); assertEquals(1f, first[100]); assertEquals(140f, first[239])
    val second = FloatArray(GeneratorAudio.BLOCK)
    assertFalse(GeneratorAudio.mix(second, 1240, packet, 1100))
    assertEquals(141f, second[0]); assertEquals(300f, second[159]); assertEquals(0f, second[160])
    // Not begun yet: nothing written, still pending.
    val before = FloatArray(GeneratorAudio.BLOCK)
    assertTrue(GeneratorAudio.mix(before, 0, packet, 5000))
    assertTrue(before.all { it == 0f })
  }
  @Test fun rasterRotatesInPortrait() {
    val portrait = GeneratorLayout.fitRaster(1000f, 2000f)
    assertTrue(portrait.rotate); assertEquals(1000f / 720, portrait.scale, 1e-6f)
    val landscape = GeneratorLayout.fitRaster(2560f, 1000f)
    assertFalse(landscape.rotate); assertEquals(1000f / 720, landscape.scale, 1e-6f)
    assertEquals(1f, GeneratorLayout.fitRaster(1280f, 720f).scale, 1e-6f)
  }
  @Test fun calibrationPatternInMillimetres() {
    val mm = GeneratorLayout.pixelsPerMm(25.4f * 16, 480f) // 16 px per mm
    assertEquals(16f, mm, 1e-4f)
    assertEquals(480f / 25.4f, GeneratorLayout.pixelsPerMm(0f, 480f), 1e-4f)
    val l = GeneratorLayout.calibration(1080f, 2400f, mm)
    assertEquals(2400f - 62 * 16, l.top, 1e-3f)
    // Board 22 × 66 mm centred on the pattern line; sensors (5 mm from the notch end) at the middle of the halves.
    assertEquals(22f * 16, l.boardRight - l.boardLeft, 1e-3f); assertEquals(66f * 16, l.boardBottom - l.boardTop, 1e-3f)
    assertEquals(540f, (l.boardLeft + l.boardRight) / 2, 1e-3f)
    assertEquals((l.top + 2400f) / 2, l.boardBottom - 5 * 16, 1e-3f)
    assertEquals(l.boardBottom + 2 * 16, l.notchTick, 1e-3f)
    // A short view: the halves fill it.
    assertEquals(0f, GeneratorLayout.calibration(1080f, 500f, mm).top, 0f)
  }
  @Test fun clockCheckMapsToTheHostClock() {
    // A check on PhoneClock 1 s before now: UTC minus host = (unix - boot) / 1000 + (boot - host).
    val check = mapOf("clockId" to PhoneClock.identifier, "clockUnixMs" to "1800000000000", "clockMonotonicMs" to "50000")
    assertEquals(1_799_999_950.0 + 2.5, GeneratorClock.referenceMapping(check, 51_000.0, 2.5)!!, 1e-6)
    assertNull(GeneratorClock.referenceMapping(check, 350_000.0, 2.5))   // 300 s old
    assertNull(GeneratorClock.referenceMapping(check, 49_000.0, 2.5))    // from the future
    assertNull(GeneratorClock.referenceMapping(check + ("clockId" to "other"), 51_000.0, 2.5))
    assertNull(GeneratorClock.referenceMapping(check + ("clockUnixMs" to "17"), 51_000.0, 2.5))
  }
}
