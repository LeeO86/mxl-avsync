package io.leeo86.mxl.avsync

import java.nio.ByteBuffer

// Kotlin handle on avsync::PhoneAnalyzer (core/include/avsync/phone_analyzer.hpp). Not thread-safe:
// the camera view touches it from its capture thread only; a snapshot is owned by one worker.
class PhoneAnalyzer private constructor(private var handle: Long) : AutoCloseable {
  constructor() : this(create())

  // One calibration take. Times are relative to the take start; luma frames are width × height.
  class CalibrationTake(val frameTimesMs: DoubleArray, val widths: IntArray, val heights: IntArray, val luma: Array<ByteArray>,
    val audio: FloatArray, val sampleRate: Int, val audioStartMs: Double, val spikeMs: Double, val fps: Double)

  fun reset() = reset(live())
  fun setTaiUtcSeconds(seconds: Double) = setTaiUtcSeconds(live(), seconds)
  fun setDeviceUtcOffset(unixMinusCaptureMs: Double) = setDeviceUtcOffset(live(), unixMinusCaptureMs)
  // corners: x TL, TR, BR, BL then y in the same order, normalized to the camera image.
  fun observeQr(payload: String, corners: DoubleArray, captureMs: Double) = observeQr(live(), payload, corners, captureMs)
  fun qrScanSuggested(captureMs: Double) = qrScanSuggested(live(), captureMs)
  fun pushVideo(luma: ByteBuffer, width: Int, height: Int, stride: Int, ptsMs: Double, exposureMs: Double, readoutMs: Double) =
    pushVideo(live(), luma, width, height, stride, ptsMs, exposureMs, readoutMs)
  fun pushAudio(mono: FloatArray, count: Int, ptsMs: Double, sampleRate: Int) = pushAudio(live(), mono, count, ptsMs, sampleRate)
  fun reportDue() = reportDue(live())
  fun poll(): String? = poll(live())
  fun snapshotForPoll() = PhoneAnalyzer(snapshotForPoll(live()))
  fun mergeAudio(analyzed: PhoneAnalyzer) = mergeAudio(live(), analyzed.live())
  fun beginCalibration() = beginCalibration(live())
  fun calibrating() = calibrating(live())
  fun calibrationError(): String? = calibrationError(live())
  fun takeCalibration(): CalibrationTake? = takeCalibration(live())
  // Calibrator board take: "camera" or "display" (docs/calibrator.md); pollCalibrator gives JSON reports.
  fun beginCalibrator(mode: String) = beginCalibrator(live(), mode == "display")
  fun calibratorActive() = calibratorActive(live())
  fun pollCalibrator(): String? = pollCalibrator(live())
  fun picture(): DoubleArray? = picture(live())

  override fun close() { if (handle != 0L) { destroy(handle); handle = 0 } }
  private fun live(): Long { check(handle != 0L) { "PhoneAnalyzer closed" }; return handle }

  private companion object {
    init { System.loadLibrary("avsync-jni") }
    @JvmStatic external fun create(): Long
    @JvmStatic external fun destroy(h: Long)
    @JvmStatic external fun reset(h: Long)
    @JvmStatic external fun setTaiUtcSeconds(h: Long, seconds: Double)
    @JvmStatic external fun setDeviceUtcOffset(h: Long, ms: Double)
    @JvmStatic external fun observeQr(h: Long, payload: String, corners: DoubleArray, captureMs: Double)
    @JvmStatic external fun qrScanSuggested(h: Long, captureMs: Double): Boolean
    @JvmStatic external fun pushVideo(h: Long, luma: ByteBuffer, width: Int, height: Int, stride: Int, pts: Double, exposure: Double, readout: Double)
    @JvmStatic external fun pushAudio(h: Long, samples: FloatArray, count: Int, pts: Double, rate: Int)
    @JvmStatic external fun reportDue(h: Long): Boolean
    @JvmStatic external fun poll(h: Long): String?
    @JvmStatic external fun snapshotForPoll(h: Long): Long
    @JvmStatic external fun mergeAudio(h: Long, analyzed: Long)
    @JvmStatic external fun beginCalibration(h: Long)
    @JvmStatic external fun calibrating(h: Long): Boolean
    @JvmStatic external fun calibrationError(h: Long): String?
    @JvmStatic external fun takeCalibration(h: Long): CalibrationTake?
    @JvmStatic external fun beginCalibrator(h: Long, display: Boolean)
    @JvmStatic external fun calibratorActive(h: Long): Boolean
    @JvmStatic external fun pollCalibrator(h: Long): String?
    @JvmStatic external fun picture(h: Long): DoubleArray?
  }
}
