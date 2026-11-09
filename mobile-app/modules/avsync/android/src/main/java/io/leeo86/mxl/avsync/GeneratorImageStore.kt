package io.leeo86.mxl.avsync

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.ImageDecoder
import android.net.Uri
import android.os.Build
import java.io.File
import java.util.UUID

// A small app-owned copy (PNG, at most 512 px) of the image chosen for the generator, like the iOS
// GeneratorImageStore. A UUID names it, so callers cannot hand the renderer an arbitrary path.
object GeneratorImageStore {
  private const val MAX_SIDE = 512
  private val ID = Regex("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$")

  private fun file(context: Context, id: String): File {
    require(ID.matches(id)) { "Invalid generator image ID." }
    return File(File(context.filesDir, "avsync-generator").apply { mkdirs() }, "$id.png")
  }

  fun load(context: Context, id: String): Bitmap? {
    if (id.isEmpty()) return null
    return BitmapFactory.decodeFile(file(context, id).path) ?: throw IllegalStateException("Saved image is unavailable. Choose it again.")
  }

  fun info(context: Context, id: String): Map<String, Any>? {
    val image = runCatching { load(context, id) }.getOrNull() ?: return null
    return mapOf("id" to id, "uri" to Uri.fromFile(file(context, id)).toString(), "width" to image.width.toDouble(), "height" to image.height.toDouble())
  }

  // Decodes the picked image (EXIF rotation applied on Android 9 and newer), scales it down and saves it.
  fun save(context: Context, uri: Uri): Map<String, Any> {
    val decoded = if (Build.VERSION.SDK_INT >= 28) {
      ImageDecoder.decodeBitmap(ImageDecoder.createSource(context.contentResolver, uri)) { decoder, info, _ ->
        val scale = minOf(1.0, MAX_SIDE.toDouble() / maxOf(info.size.width, info.size.height))
        decoder.setTargetSize(maxOf(1, Math.round(info.size.width * scale).toInt()), maxOf(1, Math.round(info.size.height * scale).toInt()))
        decoder.allocator = ImageDecoder.ALLOCATOR_SOFTWARE
      }
    } else {
      val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
      context.contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, bounds) }
      var sample = 1
      while (maxOf(bounds.outWidth, bounds.outHeight) / (sample * 2) >= MAX_SIDE) sample *= 2
      context.contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, BitmapFactory.Options().apply { inSampleSize = sample }) }
    } ?: throw IllegalStateException("The selected image could not be imported.")
    if (decoded.width <= 0 || decoded.height <= 0) throw IllegalStateException("The selected image is empty.")
    val scale = minOf(1.0, MAX_SIDE.toDouble() / maxOf(decoded.width, decoded.height))
    val image = if (scale < 1) Bitmap.createScaledBitmap(decoded, maxOf(1, Math.round(decoded.width * scale).toInt()), maxOf(1, Math.round(decoded.height * scale).toInt()), true) else decoded
    val id = UUID.randomUUID().toString()
    val target = file(context, id)
    val temporary = File(target.path + ".tmp")
    temporary.outputStream().use { if (!image.compress(Bitmap.CompressFormat.PNG, 100, it)) throw IllegalStateException("The selected image could not be imported.") }
    if (!temporary.renameTo(target)) throw IllegalStateException("The selected image could not be saved.")
    return mapOf("id" to id, "uri" to Uri.fromFile(target).toString(), "width" to image.width.toDouble(), "height" to image.height.toDouble())
  }

  fun remove(context: Context, id: String) { runCatching { file(context, id).delete() } }
}
