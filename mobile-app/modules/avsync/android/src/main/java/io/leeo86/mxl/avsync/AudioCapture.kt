package io.leeo86.mxl.avsync

import android.annotation.SuppressLint
import android.media.AudioDeviceInfo
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioRecord
import android.media.AudioTimestamp
import android.media.MediaRecorder
import android.os.SystemClock

// Microphone capture, mono float at 48 kHz, stamped on CLOCK_BOOTTIME (the camera's REALTIME clock).
// Each 10 ms block is handed to `onSamples` with the boot time of its first sample in ms.
class AudioCapture(private val audioManager: AudioManager, private val onSamples: (FloatArray, Int, Double) -> Unit) {
  val sampleRate = 48_000
  @Volatile var route = "unknown"; private set
  // 'boottime' when AudioRecord.getTimestamp works, else 'arrival-estimated' (late by the input buffer).
  @Volatile var clock = "boottime"; private set
  @Volatile private var running = false
  private var thread: Thread? = null

  @SuppressLint("MissingPermission") // The view checks RECORD_AUDIO before starting.
  fun start() {
    val unprocessed = audioManager.getProperty(AudioManager.PROPERTY_SUPPORT_AUDIO_SOURCE_UNPROCESSED) == "true"
    val format = AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_FLOAT).setSampleRate(sampleRate)
      .setChannelMask(AudioFormat.CHANNEL_IN_MONO).build()
    val minimum = AudioRecord.getMinBufferSize(sampleRate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_FLOAT)
    val record = AudioRecord.Builder()
      .setAudioSource(if (unprocessed) MediaRecorder.AudioSource.UNPROCESSED else MediaRecorder.AudioSource.VOICE_RECOGNITION)
      .setAudioFormat(format).setBufferSizeInBytes(maxOf(minimum * 2, sampleRate / 10 * 4)).build()
    if (record.state != AudioRecord.STATE_INITIALIZED) { record.release(); throw IllegalStateException("Microphone unavailable on this device.") }
    running = true
    thread = Thread({ read(record) }, "avsync.audio").apply { priority = Thread.MAX_PRIORITY; start() }
  }

  fun stop() {
    running = false
    thread?.join(500)
    thread = null
  }

  private fun read(record: AudioRecord) {
    val block = sampleRate / 100
    val stamp = AudioTimestamp()
    var position = 0L
    try {
      record.startRecording()
      route = record.routedDevice?.let { routeName(it.type) } ?: "unknown"
      while (running) {
        val samples = FloatArray(block)
        val n = record.read(samples, 0, block, AudioRecord.READ_BLOCKING)
        if (n <= 0) { if (n < 0) break else continue }
        val ptsMs = if (record.getTimestamp(stamp, AudioTimestamp.TIMEBASE_BOOTTIME) == AudioRecord.SUCCESS) {
          clock = "boottime"
          (stamp.nanoTime + (position - stamp.framePosition) * 1e9 / sampleRate) / 1e6
        } else {
          clock = "arrival-estimated"
          SystemClock.elapsedRealtimeNanos() / 1e6 - n * 1000.0 / sampleRate
        }
        position += n
        onSamples(samples, n, ptsMs)
      }
    } finally {
      runCatching { record.stop() }
      record.release()
    }
  }

  private fun routeName(type: Int) = when (type) {
    AudioDeviceInfo.TYPE_BUILTIN_MIC -> "builtin-mic"
    AudioDeviceInfo.TYPE_WIRED_HEADSET -> "wired-headset"
    AudioDeviceInfo.TYPE_USB_DEVICE, AudioDeviceInfo.TYPE_USB_HEADSET -> "usb"
    AudioDeviceInfo.TYPE_BLUETOOTH_SCO -> "bluetooth-sco"
    else -> "type-$type"
  }
}
