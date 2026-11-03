package io.leeo86.mxl.avsync

import android.graphics.Bitmap
import android.net.Uri
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.roundToInt

// Writes a calibration take as JPEG frames and a WAV file, and builds the `calibration` report.
object CalibrationFiles {
  fun wav(samples: FloatArray, rate: Int): ByteArray {
    val length = samples.size * 2
    val out = ByteBuffer.allocate(44 + length).order(ByteOrder.LITTLE_ENDIAN)
    out.put("RIFF".toByteArray()).putInt(36 + length).put("WAVEfmt ".toByteArray()).putInt(16)
      .putShort(1).putShort(1).putInt(rate).putInt(rate * 2).putShort(2).putShort(16)
      .put("data".toByteArray()).putInt(length)
    for (x in samples) out.putShort((x.coerceIn(-1f, 1f) * 32767).roundToInt().toShort())
    return out.array()
  }

  // Peaks of about 256 equal buckets; the bin duration is reported next to them.
  fun waveform(samples: FloatArray): Pair<List<Double>, Int> {
    val bucket = max(1, samples.size / 256)
    val peaks = (samples.indices step bucket).map { start ->
      var peak = 0f
      for (i in start until minOf(samples.size, start + bucket)) peak = max(peak, abs(samples[i]))
      peak.toDouble()
    }
    return peaks to bucket
  }

  fun write(take: PhoneAnalyzer.CalibrationTake, directory: File): Map<String, Any> {
    directory.deleteRecursively()
    if (!directory.mkdirs()) throw IllegalStateException("Could not save calibration frames. Check available storage and repeat.")
    val frames = take.luma.indices.map { i ->
      val w = take.widths[i]; val h = take.heights[i]; val gray = take.luma[i]
      val pixels = IntArray(w * h) { val v = gray[it].toInt() and 0xff; (0xff shl 24) or (v shl 16) or (v shl 8) or v }
      val file = File(directory, "$i.jpg")
      val bitmap = Bitmap.createBitmap(pixels, w, h, Bitmap.Config.ARGB_8888)
      file.outputStream().use { if (!bitmap.compress(Bitmap.CompressFormat.JPEG, 85, it)) throw IllegalStateException("Could not save calibration frames. Check available storage and repeat.") }
      bitmap.recycle()
      mapOf("timeMs" to take.frameTimesMs[i], "uri" to Uri.fromFile(file).toString())
    }
    val sound = File(directory, "sound.wav")
    sound.writeBytes(wav(take.audio, take.sampleRate))
    val (peaks, bucket) = waveform(take.audio)
    val before = take.frameTimesMs.count { it < take.spikeMs }
    return mapOf("kind" to "calibration", "frames" to frames, "spikeTimeMs" to take.spikeMs, "fps" to take.fps,
      "framesBefore" to before, "framesAfter" to take.frameTimesMs.size - before,
      "waveform" to peaks, "audioStartMs" to take.audioStartMs, "waveformStartMs" to take.audioStartMs,
      "waveformBinDurationMs" to bucket * 1000.0 / take.sampleRate, "waveformDurationMs" to take.audio.size * 1000.0 / take.sampleRate,
      "uri" to Uri.fromFile(sound).toString())
  }
}
