package io.leeo86.mxl.avsync

import android.graphics.Bitmap

// Kotlin handle on avsync::PhoneGeneratorPlan and the shared picture renderer (generator_jni.cpp),
// like ios/AVSyncGenerator.mm. image() may run on a worker while frame() runs on the main thread.
class PhoneGenerator(testId: String, rate: Int, epochTaiNs: Long, epochHostSeconds: Double, source: String) : AutoCloseable {
  private var handle = create(testId, rate, epochTaiNs, epochHostSeconds, source).also { require(it != 0L) { "Invalid generator plan." } }

  // One display frame: what the layers show at this host time.
  class Frame(val sequence: Long, val position: Int, val aWhite: Boolean, val named: Long,
    val syncX: Float, val syncY: Float, val syncSize: Float, val syncColor: Int)

  fun setSource(name: String, subtitle: String, image: Bitmap?) {
    val still = image?.let { argb(it) }
    setSource(live(), name, subtitle, still, image?.width ?: 0, image?.height ?: 0)
  }
  fun setIcon(icon: Bitmap) = setIcon(live(), argb(icon), icon.width, icon.height)
  fun setSenderCalibration(method: String, offsetMs: Double, uncertaintyMs: Double, day: Int) = setSenderCalibration(live(), method, offsetMs, uncertaintyMs, day)
  // [markerX, markerY, markerSide] in the 1280 × 720 raster.
  fun geometry(): IntArray = geometry(live())
  fun syncForeground(): Bitmap = bitmap(syncForeground(live()))
  fun frame(targetHostSeconds: Double): Frame {
    val v = frame(live(), targetHostSeconds)
    return Frame(v[0].toLong(), v[1].toInt(), v[2] != 0.0, v[3].toLong(), v[4].toFloat(), v[5].toFloat(), v[6].toFloat(),
      android.graphics.Color.rgb(v[7].toInt(), v[8].toInt(), v[9].toInt()))
  }
  fun hostTime(sequence: Long): Double = hostTime(live(), sequence)
  fun audio(sequence: Long): FloatArray = audio(live(), sequence)
  // Clock refresh (core PhoneGeneratorPlan::retime): stamps of events from fromSequence on slew to offsetNs;
  // false (nothing changes) when that is more than maxStepNs away from the current stamps.
  fun retime(offsetNs: Long, fromSequence: Long, maxStepNs: Long): Boolean = retime(live(), offsetNs, fromSequence, maxStepNs)
  fun clockOffsetMs(sequence: Long): Double = clockOffsetMs(live(), sequence)
  fun image(sequence: Long): Bitmap = bitmap(image(live(), sequence) ?: throw IllegalStateException("Session exceeds one day."))

  override fun close() { if (handle != 0L) { destroy(handle); handle = 0 } }
  private fun live(): Long { check(handle != 0L) { "PhoneGenerator closed" }; return handle }

  companion object {
    const val WIDTH = 1280
    const val HEIGHT = 720
    init { System.loadLibrary("avsync-jni") }

    // A still at most 512 px on the long side, as ARGB ints (straight alpha), like the iOS bridge.
    private fun argb(image: Bitmap): IntArray {
      val scale = minOf(1.0, 512.0 / maxOf(image.width, image.height))
      val small = if (scale < 1) Bitmap.createScaledBitmap(image, maxOf(1, Math.round(image.width * scale).toInt()), maxOf(1, Math.round(image.height * scale).toInt()), true) else image
      return IntArray(small.width * small.height).also { small.getPixels(it, 0, small.width, 0, 0, small.width, small.height) }
    }
    private fun bitmap(argb: IntArray) = Bitmap.createBitmap(argb, WIDTH, HEIGHT, Bitmap.Config.ARGB_8888)

    // The clock reference rule (core clock_reference_expired): older than 300 s or radius + 50 ppm holdover above the limit.
    @JvmStatic external fun clockReferenceExpired(ageMs: Double, radiusMs: Double, limitMs: Double): Boolean
    @JvmStatic external fun testId(prefix: String, installId: String, version: String, settings: Array<String>): String
    // Calibration pattern (docs/calibrator.md display mode), the same core functions as every generator.
    @JvmStatic external fun calibrationLeftWhite(frame: Long, fps: Int): Boolean
    @JvmStatic external fun calibrationSwapFrame(swap: Long, fps: Int): Long
    @JvmStatic external fun calibrationBeep(sampleRate: Int): FloatArray
    // Clicks and beeps in mono samples: JSON with ok, pairs, clicks and, when ok, offsetMs, uncertaintyMs, …
    @JvmStatic external fun displayCalibration(samples: FloatArray, count: Int, sampleRate: Int, minimumPairs: Int): String

    @JvmStatic private external fun create(testId: String, rate: Int, epochTaiNs: Long, epochHost: Double, source: String): Long
    @JvmStatic private external fun destroy(h: Long)
    @JvmStatic private external fun setSource(h: Long, name: String, subtitle: String, argb: IntArray?, width: Int, height: Int)
    @JvmStatic private external fun setIcon(h: Long, argb: IntArray, width: Int, height: Int)
    @JvmStatic private external fun setSenderCalibration(h: Long, method: String, offset: Double, uncertainty: Double, day: Int)
    @JvmStatic private external fun geometry(h: Long): IntArray
    @JvmStatic private external fun syncForeground(h: Long): IntArray
    @JvmStatic private external fun frame(h: Long, target: Double): DoubleArray
    @JvmStatic private external fun hostTime(h: Long, seq: Long): Double
    @JvmStatic private external fun audio(h: Long, seq: Long): FloatArray
    @JvmStatic private external fun image(h: Long, seq: Long): IntArray?
    @JvmStatic private external fun retime(h: Long, offsetNs: Long, fromSeq: Long, maxStepNs: Long): Boolean
    @JvmStatic private external fun clockOffsetMs(h: Long, seq: Long): Double
  }
}
