package io.leeo86.mxl.avsync
import org.junit.Test
import org.junit.Assert.*
import java.nio.ByteBuffer
import java.nio.ByteOrder
class CaptureTest {
  private fun out(w: Int, h: Int, fps: Int) = CaptureProfile.Output(CaptureProfile.Size(w, h), 1_000_000_000L / fps)
  private val outputs = listOf(out(4000, 3000, 30), out(1920, 1080, 60), out(1280, 720, 120), out(640, 480, 240))
  @Test fun picksHighestRateAtLeast720p() {
    val ranges = listOf(15 to 30, 30 to 30, 60 to 60, 30 to 120, 120 to 120)
    val auto = CaptureProfile.choose(ranges, outputs, 0)!!
    assertEquals(120, auto.fps); assertEquals(CaptureProfile.Size(1280, 720), auto.size); assertEquals(120, auto.aeMin)
    val sixty = CaptureProfile.choose(ranges, outputs, 60)!!
    assertEquals(60, sixty.fps); assertEquals(CaptureProfile.Size(1920, 1080), sixty.size)
    // 240 only exists below 720p: fall back to the next rate.
    assertEquals(120, CaptureProfile.choose(ranges + (240 to 240), outputs, 240)!!.fps)
    assertNull(CaptureProfile.choose(listOf(30 to 30), listOf(out(640, 480, 30)), 0))
  }
  @Test fun normalSessionTakes90And120() {
    // Option 1: the normal session lists 90 or 120 fps; 120 at 1080p is listed as 8 333 334 ns.
    val fast = listOf(out(4000, 3000, 30), CaptureProfile.Output(CaptureProfile.Size(1920, 1080), 8_333_334), out(1280, 720, 120))
    val ranges = listOf(15 to 30, 30 to 30, 30 to 90, 90 to 90, 120 to 120)
    val auto = CaptureProfile.choose(ranges, fast, 0)!!
    assertEquals(120, auto.fps); assertEquals(CaptureProfile.Size(1920, 1080), auto.size); assertFalse(auto.highSpeed)
    val ninety = CaptureProfile.choose(listOf(30 to 30, 30 to 90, 90 to 90), outputs, 0)!!
    assertEquals(90, ninety.fps); assertEquals(90, ninety.aeMin); assertEquals(CaptureProfile.Size(1280, 720), ninety.size)
    // 90 fps only as a variable range: still 90, with the range's lower bound.
    assertEquals(30, CaptureProfile.choose(listOf(30 to 30, 30 to 90), outputs, 120)!!.aeMin)
    // A size whose minimum frame duration is too long for 120 is skipped.
    assertEquals(CaptureProfile.Size(1280, 720), CaptureProfile.choose(listOf(120 to 120), outputs, 120)!!.size)
  }
  @Test fun highSpeedOnlyAboveTheNormalRate() {
    val hs = listOf(
      CaptureProfile.HighSpeed(CaptureProfile.Size(1920, 1080), listOf(30 to 120, 120 to 120)),
      CaptureProfile.HighSpeed(CaptureProfile.Size(1280, 720), listOf(30 to 120, 120 to 120, 30 to 240, 240 to 240)),
      CaptureProfile.HighSpeed(CaptureProfile.Size(640, 480), listOf(480 to 480)))
    val auto = CaptureProfile.chooseHighSpeed(hs, 0, 60)!!
    assertEquals(240, auto.fps); assertEquals(CaptureProfile.Size(1280, 720), auto.size); assertEquals(240, auto.aeMin); assertTrue(auto.highSpeed)
    val oneTwenty = CaptureProfile.chooseHighSpeed(hs, 120, 60)!!
    assertEquals(120, oneTwenty.fps); assertEquals(CaptureProfile.Size(1920, 1080), oneTwenty.size)
    // Not when the normal session already reaches the rate, nor below 120 or below 720p.
    assertNull(CaptureProfile.chooseHighSpeed(hs, 120, 120))
    assertNull(CaptureProfile.chooseHighSpeed(hs, 60, 30))
    assertEquals(240, CaptureProfile.chooseHighSpeed(hs, 240, 120)!!.fps)
    assertNull(CaptureProfile.chooseHighSpeed(listOf(CaptureProfile.HighSpeed(CaptureProfile.Size(640, 480), listOf(240 to 240))), 0, 0))
    // Only variable ranges listed: no constant interval, so no high-speed mode.
    assertNull(CaptureProfile.chooseHighSpeed(listOf(CaptureProfile.HighSpeed(CaptureProfile.Size(1280, 720), listOf(30 to 120))), 0, 60))
  }
  @Test fun textureCropFromTransform() {
    // Identity-like GL flip, full buffer.
    val flip = floatArrayOf(1f, 0f, 0f, 0f, 0f, -1f, 0f, 0f, 0f, 0f, 1f, 0f, 0f, 1f, 0f, 1f)
    assertArrayEquals(floatArrayOf(0f, 0f, 1f, 1f), HighSpeedCapture.crop(flip), 1e-6f)
    // 90° rotation plus a crop of 1080 of 1088 rows: the box is the same whatever the rotation.
    val s = 1080f / 1088f
    val rotated = floatArrayOf(0f, -s, 0f, 0f, -1f, 0f, 0f, 0f, 0f, 0f, 1f, 0f, 1f, s, 0f, 1f)
    assertArrayEquals(floatArrayOf(0f, 0f, 1f, s), HighSpeedCapture.crop(rotated), 1e-6f)
  }
  @Test fun fixedExposureKeepsBrightness() {
    // 1/250 s at ISO 400 → the full 1/60 s interval at ISO 96.
    val (exposure, iso) = CaptureProfile.fixedExposure(4_000_000, 400, 16_666_666, 10_000L..100_000_000L, 50..3200)
    assertEquals(16_666_666L, exposure); assertEquals(96, iso)
    assertEquals(50, CaptureProfile.fixedExposure(1_000_000, 100, 16_666_666, 10_000L..100_000_000L, 50..3200).second)
    assertEquals(10_000_000L, CaptureProfile.fixedExposure(1_000_000, 100, 16_666_666, 10_000L..10_000_000L, 50..3200).first)
  }
  @Test fun sntpDecodesAndRejects() {
    val t1 = 1_793_450_000_000.0
    val request = SntpPacket.request(t1, 7)
    fun stamp(ms: Double): ByteArray {
      val s = ms / 1000 + SntpPacket.EPOCH; val whole = Math.floor(s)
      return ByteBuffer.allocate(8).putInt(whole.toLong().toInt()).putInt(((s - whole) * 4_294_967_296.0).toLong().toInt()).array()
    }
    val reply = ByteBuffer.allocate(48).order(ByteOrder.BIG_ENDIAN)
    reply.put(0, 0x24).put(1, 2).put(3, (-20).toByte())
    request.copyInto(reply.array(), 24, 40, 48)
    stamp(t1 + 20).copyInto(reply.array(), 32); stamp(t1 + 21).copyInto(reply.array(), 40)
    // Server 10 ms ahead, 10 ms each way, 1 ms processing.
    val sample = SntpPacket.decode(reply.array(), request, t1, t1 + 21, 21.0)
    assertEquals(10.0, sample.offsetMs, .01); assertEquals(20.0, sample.networkRoundTripMs, .01)
    val wrong = reply.array().copyOf(); wrong[24] = (wrong[24] + 1).toByte()
    assertThrows(SntpPacket.Error::class.java) { SntpPacket.decode(wrong, request, t1, t1 + 21, 21.0) }
    assertThrows(SntpPacket.Error::class.java) { SntpPacket.decode(reply.array(), request, t1, t1 + 100, 21.0) }
    val sampleMap = { o: Double, u: Double -> mapOf<String, Any>("offsetMs" to o, "uncertaintyMs" to u) }
    assertEquals(10.0, SntpPacket.best(listOf(sampleMap(12.0, 5.0), sampleMap(10.0, 2.0)))["offsetMs"] as Double, 0.0)
    assertThrows(SntpPacket.Error::class.java) { SntpPacket.best(listOf(sampleMap(40.0, 5.0), sampleMap(10.0, 2.0))) }
  }
  @Test fun wavAndWaveform() {
    val audio = FloatArray(48_000) { if (it == 1000) 1f else 0f }
    val wav = ByteBuffer.wrap(CalibrationFiles.wav(audio, 48_000)).order(ByteOrder.LITTLE_ENDIAN)
    assertEquals(44 + 96_000, wav.capacity()); assertEquals(48_000, wav.getInt(24)); assertEquals(32767.toShort(), wav.getShort(44 + 2000))
    val (peaks, bucket) = CalibrationFiles.waveform(audio)
    assertEquals(187, bucket); assertEquals(1.0, peaks[1000 / 187], 0.0); assertTrue(peaks.size in 256..257)
  }
}
