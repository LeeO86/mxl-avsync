package io.leeo86.mxl.avsync

import android.os.SystemClock
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import kotlin.math.abs
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.pow
import kotlin.math.roundToLong

// RFC 5905 four-timestamp exchange, a port of ios/SntpPacket.swift. No wall-clock adjustment.
object SntpPacket {
  const val EPOCH = 2_208_988_800.0
  class Sample(val offsetMs: Double, val uncertaintyMs: Double, val networkRoundTripMs: Double)
  class Error(message: String) : Exception(message)

  fun request(unixMs: Double, nonce: Int): ByteArray {
    val bytes = ByteArray(48); bytes[0] = 0x23 // version 4, client
    val seconds = unixMs / 1000 + EPOCH
    val whole = floor(seconds)
    val sec = whole.toLong(); val fraction = ((seconds - whole) * 4_294_967_296.0).toLong()
    for (i in 0 until 4) { bytes[40 + i] = (sec shr (24 - i * 8)).toByte(); bytes[44 + i] = (fraction shr (24 - i * 8)).toByte() }
    // A nonce below the timestamp resolution; prevents accepting a stale reply with a rounded T1.
    bytes[47] = nonce.toByte()
    return bytes
  }
  private fun u32(b: ByteArray, i: Int) = (0 until 4).fold(0L) { v, k -> (v shl 8) or (b[i + k].toLong() and 0xff) }
  private fun time(b: ByteArray, i: Int, nearUnixMs: Double): Double {
    if ((i until i + 8).all { b[it] == 0.toByte() }) throw Error("NTP timestamp missing")
    val raw = u32(b, i) + u32(b, i + 4) / 4_294_967_296.0
    // Unfold the 2036 era rollover using the local calendar (within 68 years).
    val era = ((nearUnixMs / 1000 + EPOCH - raw) / 4_294_967_296.0).roundToLong()
    return (raw + era * 4_294_967_296.0 - EPOCH) * 1000
  }
  fun decode(b: ByteArray, request: ByteArray, sentUnixMs: Double, receivedUnixMs: Double, elapsedMs: Double): Sample {
    if (b.size < 48 || request.size != 48) throw Error("Truncated NTP packet")
    val mode = b[0].toInt() and 7; val version = (b[0].toInt() shr 3) and 7; val leap = (b[0].toInt() shr 6) and 3
    if (mode != 4 || version !in 3..4 || leap == 3) throw Error("NTP server is not synchronized")
    val stratum = b[1].toInt() and 0xff
    if (stratum !in 1..15) throw Error(if (stratum == 0) "NTP server refused the request (kiss-of-death)" else "Invalid NTP stratum")
    if (!(0 until 8).all { b[24 + it] == request[40 + it] }) throw Error("NTP reply does not match the request")
    val t1 = sentUnixMs; val t4 = receivedUnixMs
    if (!t1.isFinite() || !t4.isFinite() || !elapsedMs.isFinite() || elapsedMs < 0 || elapsedMs > 3000 ||
        abs(t4 - t1 - elapsedMs) > 5) throw Error("Device clock changed during the exchange")
    val t2 = time(b, 32, t1); val t3 = time(b, 40, t1)
    val processing = t3 - t2; val network = elapsedMs - processing
    if (processing < 0 || network < -0.1) throw Error("Invalid NTP processing interval")
    val rootDelay = u32(b, 4).toInt() / 65_536.0 * 1000
    val dispersion = u32(b, 8) / 65_536.0 * 1000
    val precision = 2.0.pow(b[3].toDouble()) * 1000
    val serverError = max(0.0, rootDelay) / 2 + dispersion + max(0.001, precision)
    if (!serverError.isFinite() || serverError > 1500) throw Error("NTP server reports excessive uncertainty")
    val offset = ((t2 - t1) + (t3 - t4)) / 2
    // Radius covers path asymmetry, server root distance, timestamp bracketing and local clock jitter.
    val uncertainty = max(0.0, network) / 2 + serverError + abs(t4 - t1 - elapsedMs) + 1
    return Sample(offset, uncertainty, max(0.0, network))
  }

  // Of all samples, the smallest uncertainty (smallest round trip); the others must agree with it.
  fun best(results: List<Map<String, Any>>): Map<String, Any> {
    val best = results.minByOrNull { it["uncertaintyMs"] as Double } ?: throw Error("NTP unavailable")
    val centre = best["offsetMs"] as Double; val radius = best["uncertaintyMs"] as Double
    if (!results.all { abs(it["offsetMs"] as Double - centre) <= it["uncertaintyMs"] as Double + radius + 1 })
      throw Error("NTP samples disagree; synchronization rejected")
    return best
  }
}

// Clock id and timebase shared by clockNow(), synchronizeClock() and the camera reports.
// The id is process-local: a saved reference must never survive a process restart or reboot.
object PhoneClock {
  val identifier: String = java.util.UUID.randomUUID().toString()
  const val TIMEBASE = "android-elapsed-realtime"
  fun nowMs() = SystemClock.elapsedRealtimeNanos() / 1e6
  fun now() = mapOf("monotonicMs" to nowMs(), "clockId" to identifier, "timebase" to TIMEBASE)
}

class SntpClient {
  private fun query(host: String): Map<String, Any> = DatagramSocket().use { socket ->
    socket.soTimeout = 3000
    val address = InetAddress.getByName(host)
    val sentMono = PhoneClock.nowMs(); val sentUnix = System.currentTimeMillis().toDouble()
    val request = SntpPacket.request(sentUnix, (0..255).random())
    socket.send(DatagramPacket(request, request.size, address, 123))
    val reply = ByteArray(68); val packet = DatagramPacket(reply, reply.size)
    try { socket.receive(packet) } catch (_: java.net.SocketTimeoutException) { throw SntpPacket.Error("NTP reply timed out") }
    val receivedMono = PhoneClock.nowMs(); val receivedUnix = System.currentTimeMillis().toDouble()
    val sample = SntpPacket.decode(reply.copyOf(packet.length), request, sentUnix, receivedUnix, receivedMono - sentMono)
    mapOf("source" to "ntp", "server" to host, "referenceUnixMs" to receivedUnix + sample.offsetMs,
      "referenceMonotonicMs" to receivedMono, "deviceUnixMs" to receivedUnix,
      "offsetMs" to sample.offsetMs, "uncertaintyMs" to sample.uncertaintyMs, "roundTripMs" to sample.networkRoundTripMs,
      "clockId" to PhoneClock.identifier, "timebase" to PhoneClock.TIMEBASE, "authenticated" to false,
      "uncertaintyModel" to "network-asymmetry + server-root-distance + local-stamping")
  }

  // Called on a worker. Port is fixed at 123.
  fun synchronize(host: String, samples: Int = 8): Map<String, Any> {
    if (host.isEmpty() || host.length > 253 || (host.contains(':') && host.count { it == ':' } < 2) ||
        !Regex("^[A-Za-z0-9:.\\-]+$").matches(host)) throw SntpPacket.Error("Enter an NTP hostname or IP address, without a URL or port")
    val results = mutableListOf<Map<String, Any>>()
    var lastError: Exception? = null
    for (i in 0 until samples) {
      if (i > 0) Thread.sleep(250)
      try { results.add(query(host)) }
      catch (e: Exception) { lastError = e; if (e.message?.contains("kiss-of-death") == true) break }
    }
    if (results.isEmpty()) throw lastError ?: SntpPacket.Error("NTP unavailable")
    val best = SntpPacket.best(results)
    return best + mapOf("samples" to results.size, "sampleAgeMs" to max(0.0, PhoneClock.nowMs() - best["referenceMonotonicMs"] as Double))
  }
}
