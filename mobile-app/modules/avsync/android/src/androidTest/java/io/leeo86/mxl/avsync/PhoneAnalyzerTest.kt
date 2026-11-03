package io.leeo86.mxl.avsync
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.nio.ByteBuffer

// JNI round trip on a device or emulator. The AVS3 measurement itself is tested in core
// (tests/unit/test_picture.cpp); this checks the bridge: frames, audio, polls, snapshots and a take.
@RunWith(AndroidJUnit4::class)
class PhoneAnalyzerTest {
  private val w = 320; private val h = 180; private val stride = 384
  private fun frame(level: Int) = ByteBuffer.allocateDirect(stride * h).apply { for (i in 0 until capacity()) put(i, level.toByte()) }

  @Test fun liveStatusThroughSnapshot() = PhoneAnalyzer().use { a ->
    a.setDeviceUtcOffset(1.7e12)
    var report: String? = null
    var audioAt = 0.0
    for (n in 0 until 180) {
      val t = 1000 + n * 1000.0 / 60
      a.pushVideo(frame(if (n % 30 < 2) 235 else 16), w, h, stride, t, 16.0, 8.0)
      while (audioAt <= t - 1000) { a.pushAudio(FloatArray(480), 480, 1000 + audioAt, 48_000); audioAt += 10 }
      if (a.reportDue()) a.snapshotForPoll().use { copy -> report = copy.poll(); a.mergeAudio(copy) }
    }
    val json = JSONObject(report!!)
    assertEquals("status", json.getString("kind"))
    assertEquals("no_signal", json.getString("stage"))
    assertEquals(1.7e12, json.getDouble("deviceUtcOffsetMs"), 1.0)
    assertTrue(json.getJSONObject("diagnostics").getInt("frames") >= 140)
    assertNull(a.picture())
  }

  @Test fun calibrationTakeAroundAClap() = PhoneAnalyzer().use { a ->
    a.beginCalibration()
    assertTrue(a.calibrating())
    var take: PhoneAnalyzer.CalibrationTake? = null
    for (block in 0 until 300) {
      val t = 1000 + block * 10.0
      val samples = FloatArray(480) { if (block == 150 && it == 100) .9f else 0f }
      a.pushAudio(samples, 480, t, 48_000)
      if (block % 2 == 0) a.pushVideo(frame(100), w, h, stride, t, 16.0, 0.0)
      take = take ?: a.takeCalibration()
    }
    assertNull(a.calibrationError())
    val result = take!!
    assertEquals(1500 + 100 / 48.0, result.spikeMs, .1)
    assertTrue(result.frameTimesMs.count { it < result.spikeMs } >= 2)
    assertEquals(result.widths[0] * result.heights[0], result.luma[0].size)
    assertEquals(48_000, result.sampleRate)
    assertTrue(result.audio.any { it > .8f })
  }
}
