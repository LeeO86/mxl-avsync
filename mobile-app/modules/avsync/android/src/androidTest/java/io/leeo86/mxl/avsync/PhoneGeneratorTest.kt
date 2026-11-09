package io.leeo86.mxl.avsync
import android.graphics.Color
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.google.android.gms.tasks.Tasks
import com.google.mlkit.vision.barcode.BarcodeScanning
import com.google.mlkit.vision.common.InputImage
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

// The generator bridge on a device or emulator: the shared renderer with Android text, the AVS4 QR,
// the audio packet, the stable ID and the calibration pattern. Timing on a real screen and speaker
// is a device test (docs/phone-generator.md).
@RunWith(AndroidJUnit4::class)
class PhoneGeneratorTest {
  private val context = InstrumentationRegistry.getInstrumentation().targetContext

  @Test fun stableIdMatchesTheCoreVector() {
    // docs/phone-generator.md: the shared test vector of generator_test_id.
    assertEquals("phone-985274814721", PhoneGenerator.testId("phone-", "00000000-0000-4000-8000-000000000000", "2.0.8", arrayOf("rate=1")))
    val id = AvsyncGenerator.stableSourceID(context, 2)
    assertEquals(id, AvsyncGenerator.stableSourceID(context, 2))
    assertNotEquals(id, AvsyncGenerator.stableSourceID(context, 1))
  }

  @Test fun pictureCarriesTheAvs4Qr() {
    GeneratorText.load(context)
    assertTrue(GeneratorText.width("AVsync", 16f) > 20)
    PhoneGenerator("phone-985274814721", 2, 1_800_000_000_000_000_000L, 100.0, "Android").use { g ->
      g.setSource("Studio A", "Camera 3", null)
      g.setSenderCalibration("transponder", -38.0, 0.5, 2100)
      val image = g.image(3)
      assertEquals(1280, image.width); assertEquals(720, image.height)
      val codes = Tasks.await(BarcodeScanning.getClient().process(InputImage.fromBitmap(image, 0)))
      val text = codes.mapNotNull { it.rawValue }.first { it.startsWith("AVS4:") }
      assertTrue(text.isNotEmpty())
      // Event timing: event 3 at 2/s is 1 s after the epoch.
      assertEquals(101.0, g.hostTime(3), 1e-9)
      assertTrue(g.audio(3).size > 1000)
      // Frame 60 (1 s after the epoch) belongs to event 3, position 0.
      val frame = g.frame(101.0 + 1e-6)
      assertEquals(3L, frame.sequence); assertEquals(0, frame.position)
      val foreground = g.syncForeground()
      assertTrue((0 until 720 step 4).any { y -> (0 until 1280 step 4).any { x -> foreground.getPixel(x, y) == Color.WHITE } })
    }
  }

  @Test fun calibrationPatternAndAnalysis() {
    // Period k lasts 500 ms: at 60 fps the swaps are every 30 frames, left half white in even periods.
    assertTrue(PhoneGenerator.calibrationLeftWhite(0, 60)); assertFalse(PhoneGenerator.calibrationLeftWhite(30, 60))
    assertEquals(30L, PhoneGenerator.calibrationSwapFrame(1, 60))
    val beep = PhoneGenerator.calibrationBeep(48_000)
    assertEquals(192, beep.size) // 4 ms
    // Silence: no pairs, a clear error.
    val json = JSONObject(PhoneGenerator.displayCalibration(FloatArray(48_000), 48_000, 48_000, 8))
    assertFalse(json.getBoolean("ok")); assertEquals(0, json.getInt("pairs"))
  }
}
