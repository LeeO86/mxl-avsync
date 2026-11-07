package io.leeo86.mxl.avsync

import com.google.android.gms.tasks.Tasks
import com.google.mlkit.vision.barcode.BarcodeScannerOptions
import com.google.mlkit.vision.barcode.BarcodeScanning
import com.google.mlkit.vision.barcode.common.Barcode
import com.google.mlkit.vision.common.InputImage
import java.nio.ByteBuffer

// ML Kit QR reader (bundled model) on a downscaled copy of the camera luma. Corners come back
// normalized to the camera image, x TL, TR, BR, BL then y; only AVS4 payloads count.
class QrReader : AutoCloseable {
  class Detection(val payload: String, val corners: DoubleArray)
  // width × height is the copy; step maps it back to the camera image (cameraWidth × cameraHeight).
  class Frame(val nv21: ByteArray, val width: Int, val height: Int, val step: Int, val cameraWidth: Int, val cameraHeight: Int)
  private val scanner = BarcodeScanning.getClient(BarcodeScannerOptions.Builder().setBarcodeFormats(Barcode.FORMAT_QR_CODE).build())

  // Called on the capture thread: a copy at most `limit` pixels wide, with neutral chroma.
  fun copy(luma: ByteBuffer, width: Int, height: Int, stride: Int, limit: Int = 960): Frame {
    val step = maxOf(1, (maxOf(width, height) + limit - 1) / limit)
    val w = width / step and 1.inv(); val h = height / step and 1.inv()
    val out = ByteArray(w * h * 3 / 2)
    val row = ByteArray(stride)
    for (y in 0 until h) {
      luma.position(y * step * stride)
      luma.get(row, 0, minOf(stride, luma.remaining()))
      for (x in 0 until w) out[y * w + x] = row[x * step]
    }
    luma.rewind()
    out.fill(128.toByte(), w * h, out.size)
    return Frame(out, w, h, step, width, height)
  }

  // Called on a worker; blocks until ML Kit is done.
  fun read(frame: Frame): List<Detection> {
    val image = InputImage.fromByteArray(frame.nv21, frame.width, frame.height, 0, InputImage.IMAGE_FORMAT_NV21)
    return Tasks.await(scanner.process(image)).mapNotNull { code ->
      val text = code.rawValue ?: return@mapNotNull null
      val points = code.cornerPoints ?: return@mapNotNull null
      if (!text.startsWith("AVS4:") || points.size != 4) return@mapNotNull null
      Detection(text, DoubleArray(8) { i -> if (i < 4) points[i].x.toDouble() * frame.step / frame.cameraWidth else points[i - 4].y.toDouble() * frame.step / frame.cameraHeight })
    }
  }

  override fun close() = scanner.close()
}
