package io.leeo86.mxl.avsync

import android.media.AudioAttributes
import android.media.AudioDeviceInfo
import android.media.AudioFormat
import android.media.AudioRouting
import android.media.AudioTimestamp
import android.media.AudioTrack
import android.os.Handler
import android.os.Looper
import android.os.Process
import java.util.concurrent.ConcurrentLinkedQueue

// The generator's speaker output: one AudioTrack stream (48 kHz mono float) that a thread keeps
// writing in 5 ms blocks, silence between packets. AudioTrack.getTimestamp says which frame leaves the
// output at which System.nanoTime (the clock of Choreographer), so a packet planned for host time T
// starts at frame position + (T − time) × 48 000: sample-exact on the output's own clock, with the
// reported output latency already inside (iOS subtracts it from the schedule time instead).
// The start frame is worked out again for every block until the packet begins, so clock drift
// between the audio and the host clock never adds up. Errors go to onError on the main thread.
class GeneratorAudio(private val onError: (String) -> Unit) {
  private class Packet(val hostNs: Long, val samples: FloatArray) { var start = -1L }
  private val queue = ConcurrentLinkedQueue<Packet>()
  private val main = Handler(Looper.getMainLooper())
  private var track: AudioTrack? = null
  private var thread: Thread? = null
  @Volatile private var running = false
  // Time from writing a frame until it plays (the audio lead); -1 until the output reports timing.
  @Volatile var latencyNs = -1L; private set
  // The output it plays on now, as in the sender calibration's audioRoute (for example builtin-speaker).
  val route: String get() = track?.routedDevice?.let { routeName(it.type) } ?: "unknown"
  val isRunning get() = running

  fun start(onRouteChange: () -> Unit) {
    val format = AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_FLOAT).setSampleRate(RATE).setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build()
    val minimum = AudioTrack.getMinBufferSize(RATE, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_FLOAT)
    // About 80 ms of buffer: timing comes from the timestamps, so a larger buffer costs nothing but safety against dropouts.
    val output = AudioTrack.Builder()
      .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build())
      .setAudioFormat(format).setTransferMode(AudioTrack.MODE_STREAM).setBufferSizeInBytes(maxOf(minimum, RATE * 4 * 80 / 1000)).build()
    if (output.state != AudioTrack.STATE_INITIALIZED) { output.release(); throw IllegalStateException("The speaker is not available.") }
    var device: Int? = null
    output.addOnRoutingChangedListener(AudioRouting.OnRoutingChangedListener { routed ->
      val now = routed.routedDevice ?: return@OnRoutingChangedListener
      if (device != null && device != now.id) onRouteChange()
      device = now.id
    }, main)
    queue.clear(); latencyNs = -1L
    track = output; running = true
    thread = Thread({ write(output) }, "avsync.generator.audio").apply { start() }
  }

  // Plays samples from host time hostNs (System.nanoTime). Call well ahead (the main thread queues 2.5 s ahead).
  fun schedule(hostNs: Long, samples: FloatArray) { queue.add(Packet(hostNs, samples)) }

  fun stop() {
    running = false
    thread?.join(500); thread = null
    track?.let { runCatching { it.pause(); it.flush(); it.stop() }; it.release() }
    track = null; queue.clear(); latencyNs = -1L
  }

  private fun fail(message: String) { running = false; main.post { onError(message) } }

  private fun write(output: AudioTrack) {
    Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO)
    val block = FloatArray(BLOCK)
    val pending = ArrayList<Packet>()
    val stamp = AudioTimestamp()
    var written = 0L
    var mapFrame = -1L; var mapNs = 0L
    var underruns = 0
    try {
      output.play()
      while (running) {
        if (output.getTimestamp(stamp) && stamp.framePosition > 0) {
          mapFrame = stamp.framePosition; mapNs = stamp.nanoTime
          latencyNs = mapNs + framesToNs(written - mapFrame) - System.nanoTime()
        }
        while (true) pending.add(queue.poll() ?: break)
        block.fill(0f)
        val iterator = pending.iterator()
        while (iterator.hasNext()) {
          val p = iterator.next()
          if (p.start < 0 || p.start >= written + 2 * BLOCK) {
            // Not about to begin: place it with the newest timestamp; it waits while the output has none.
            // Within two blocks of its start it stays put, so timestamp jitter cannot move it into the past.
            if (mapFrame < 0) continue
            p.start = startFrame(p.hostNs, mapFrame, mapNs)
            if (p.start < written) return fail("Scheduling deadline missed. Restart the signal.")
          }
          if (!mix(block, written, p.samples, p.start)) iterator.remove()
        }
        // A dropout delays everything already written: the plan is wrong from here on.
        val count = output.underrunCount
        if (mapFrame >= 0 && count > underruns) return fail("Audio dropout. Tap Start to restart.")
        underruns = count
        val n = output.write(block, 0, BLOCK, AudioTrack.WRITE_BLOCKING)
        if (n < 0) return fail("Audio output stopped. Tap Start to restart.")
        written += n
      }
    } catch (e: Exception) { fail(e.message ?: "Audio output failed.") }
  }

  companion object {
    const val RATE = 48_000
    const val BLOCK = RATE / 200 // 5 ms

    fun framesToNs(frames: Long) = frames * 1_000_000_000L / RATE
    // The output frame that plays at host time hostNs, from one timestamp (frame mapFrame plays at mapNs).
    fun startFrame(hostNs: Long, mapFrame: Long, mapNs: Long) = mapFrame + Math.round((hostNs - mapNs) * RATE / 1e9)
    // Adds the part of a packet (first frame start) that falls into the block at frame position;
    // false once the packet ends before the next block.
    fun mix(block: FloatArray, position: Long, samples: FloatArray, start: Long): Boolean {
      val from = maxOf(position, start); val to = minOf(position + block.size, start + samples.size)
      for (frame in from until to) block[(frame - position).toInt()] += samples[(frame - start).toInt()]
      return start + samples.size > position + block.size
    }
    fun routeName(type: Int) = when (type) {
      AudioDeviceInfo.TYPE_BUILTIN_SPEAKER -> "builtin-speaker"
      AudioDeviceInfo.TYPE_BUILTIN_EARPIECE -> "builtin-earpiece"
      AudioDeviceInfo.TYPE_WIRED_HEADPHONES, AudioDeviceInfo.TYPE_WIRED_HEADSET -> "wired"
      AudioDeviceInfo.TYPE_USB_DEVICE, AudioDeviceInfo.TYPE_USB_HEADSET -> "usb"
      AudioDeviceInfo.TYPE_BLUETOOTH_A2DP, AudioDeviceInfo.TYPE_BLUETOOTH_SCO -> "bluetooth"
      AudioDeviceInfo.TYPE_HDMI -> "hdmi"
      else -> "type-$type"
    }
  }
}
