package io.leeo86.mxl.avsync

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Typeface
import java.nio.ByteBuffer
import kotlin.math.ceil

// Text for the shared picture renderer (called from text_android.cpp): the bundled DejaVu Sans
// drawn by Android, as iOS draws it with CoreText. Coverage only; the renderer blends it into luma.
object GeneratorText {
  @Volatile private var typeface: Typeface = Typeface.SANS_SERIF

  fun load(context: Context) {
    runCatching { typeface = Typeface.createFromAsset(context.assets, "DejaVuSans.ttf") }
  }

  private fun paint(px: Float) = Paint(Paint.ANTI_ALIAS_FLAG).apply { typeface = this@GeneratorText.typeface; textSize = px; color = Color.WHITE }

  @JvmStatic fun width(text: String, px: Float): Int = Math.round(paint(px).measureText(text))
  @JvmStatic fun height(px: Float): Int = paint(px).fontMetrics.let { maxOf(1, ceil(it.descent - it.ascent).toInt()) }
  // Alpha coverage, row by row (rows may be padded: the caller takes size / height as the stride).
  @JvmStatic fun draw(text: String, px: Float, width: Int, height: Int): ByteArray {
    val paint = paint(px)
    val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ALPHA_8)
    Canvas(bitmap).drawText(text, 0f, -paint.fontMetrics.ascent, paint)
    val out = ByteArray(bitmap.rowBytes * height)
    bitmap.copyPixelsToBuffer(ByteBuffer.wrap(out))
    bitmap.recycle()
    return out
  }
}
