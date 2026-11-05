package io.leeo86.mxl.avsync

import android.graphics.SurfaceTexture
import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.GLES11Ext
import android.opengl.GLES20
import android.os.Handler
import android.view.Surface
import java.nio.ByteBuffer
import java.nio.ByteOrder

// Frames of a constrained high-speed session (docs/mobile-native.md). That session takes GPU
// surfaces only, so the camera writes into an OES texture. read(width) draws the latched frame
// small into an offscreen target that packs four luma pixels into one RGBA pixel and reads it
// back with glReadPixels. The texture is sampled in buffer order (only the crop of its transform
// is used), so the luma has the orientation of the normal path's ImageReader Y plane. Every
// previewEvery-th frame is also drawn upright (with the full transform) into the preview.
// Everything runs on handler's thread, which keeps the EGL context current.
class HighSpeedCapture(
  private val cameraWidth: Int, private val cameraHeight: Int, preview: SurfaceTexture, previewWidth: Int, previewHeight: Int,
  private val previewEvery: Int, handler: Handler, private val onFrame: (timestampNs: Long) -> Unit, private val onError: (String) -> Unit,
) : AutoCloseable {
  // Valid until the next frame; stride = width.
  class Luma(val buffer: ByteBuffer, val width: Int, val height: Int)
  private class Target(val width: Int, val height: Int, val texture: Int, val framebuffer: Int, val pixels: ByteBuffer)

  private val display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
  private val context: android.opengl.EGLContext
  private val window: android.opengl.EGLSurface
  private val texture: SurfaceTexture
  val surface: Surface
  private val oes: Int
  private val lumaProgram: Int
  private val previewProgram: Int
  private val quad = ByteBuffer.allocateDirect(32).order(ByteOrder.nativeOrder()).asFloatBuffer().apply { put(floatArrayOf(-1f, -1f, 1f, -1f, -1f, 1f, 1f, 1f)); rewind() }
  private val matrix = FloatArray(16)
  private var box = floatArrayOf(0f, 0f, 1f, 1f)
  private val targets = HashMap<Int, Target>()
  private val previewSize = previewWidth to previewHeight
  private var frames = 0L
  private var closed = false
  // Longest draw + glReadPixels since the last takeReadMaxMs().
  private var readMaxMs = 0.0

  init {
    val version = IntArray(2)
    check(display != EGL14.EGL_NO_DISPLAY && EGL14.eglInitialize(display, version, 0, version, 1)) { "EGL unavailable" }
    val configs = arrayOfNulls<EGLConfig>(1); val count = IntArray(1)
    val attributes = intArrayOf(EGL14.EGL_RED_SIZE, 8, EGL14.EGL_GREEN_SIZE, 8, EGL14.EGL_BLUE_SIZE, 8, EGL14.EGL_ALPHA_SIZE, 8,
      EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT, EGL14.EGL_SURFACE_TYPE, EGL14.EGL_WINDOW_BIT, EGL14.EGL_NONE)
    check(EGL14.eglChooseConfig(display, attributes, 0, configs, 0, 1, count, 0) && count[0] > 0) { "No EGL config" }
    context = EGL14.eglCreateContext(display, configs[0], EGL14.EGL_NO_CONTEXT, intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL14.EGL_NONE), 0)
    check(context != EGL14.EGL_NO_CONTEXT) { "No EGL context" }
    // The preview buffer is upright (portrait on most phones); the view's transform crops it as before.
    preview.setDefaultBufferSize(previewWidth, previewHeight)
    window = EGL14.eglCreateWindowSurface(display, configs[0], Surface(preview), intArrayOf(EGL14.EGL_NONE), 0)
    check(window != EGL14.EGL_NO_SURFACE) { "Preview surface unavailable (EGL ${EGL14.eglGetError()})" }
    check(EGL14.eglMakeCurrent(display, window, window, context)) { "EGL context not current" }
    EGL14.eglSwapInterval(display, 0)
    lumaProgram = program(LUMA)
    previewProgram = program(PREVIEW)
    oes = IntArray(1).also { GLES20.glGenTextures(1, it, 0) }[0]
    GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, oes)
    GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR)
    GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
    GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
    GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)
    texture = SurfaceTexture(oes).apply { setDefaultBufferSize(cameraWidth, cameraHeight) }
    surface = Surface(texture)
    texture.setOnFrameAvailableListener({ frame() }, handler)
    check(GLES20.glGetError() == GLES20.GL_NO_ERROR) { "GL setup failed" }
  }

  private fun frame() {
    if (closed) return
    try {
      texture.updateTexImage()
      texture.getTransformMatrix(matrix)
      box = crop(matrix)
      // SurfaceTexture.getTimestamp is the buffer's sensor timestamp, the clock of SENSOR_TIMESTAMP.
      onFrame(texture.timestamp)
      if (!closed && frames++ % previewEvery == 0L) drawPreview()
    } catch (e: RuntimeException) {
      if (!closed) onError(e.message ?: "GPU frame read failed")
    }
  }

  // The latched frame as luma about `width` pixels wide (a multiple of 4, at most the camera width).
  fun read(width: Int): Luma {
    val started = System.nanoTime()
    val w = minOf(width, cameraWidth) and 3.inv()
    val target = targets.getOrPut(w) { target(w, (w * cameraHeight / cameraWidth) and 1.inv()) }
    GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, target.framebuffer)
    GLES20.glViewport(0, 0, target.width / 4, target.height)
    GLES20.glUseProgram(lumaProgram)
    GLES20.glUniform4f(GLES20.glGetUniformLocation(lumaProgram, "box"), box[0], box[1], box[2], box[3])
    GLES20.glUniform2f(GLES20.glGetUniformLocation(lumaProgram, "size"), target.width.toFloat(), target.height.toFloat())
    draw(lumaProgram, IDENTITY)
    target.pixels.rewind()
    GLES20.glReadPixels(0, 0, target.width / 4, target.height, GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, target.pixels)
    target.pixels.rewind()
    readMaxMs = maxOf(readMaxMs, (System.nanoTime() - started) / 1e6)
    return Luma(target.pixels, target.width, target.height)
  }

  fun takeReadMaxMs() = readMaxMs.also { readMaxMs = 0.0 }

  private fun drawPreview() {
    GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, 0)
    GLES20.glViewport(0, 0, previewSize.first, previewSize.second)
    GLES20.glUseProgram(previewProgram)
    draw(previewProgram, matrix)
    EGL14.eglSwapBuffers(display, window)
  }

  private fun draw(program: Int, transform: FloatArray) {
    GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
    GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, oes)
    GLES20.glUniform1i(GLES20.glGetUniformLocation(program, "camera"), 0)
    GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(program, "transform"), 1, false, transform, 0)
    val position = GLES20.glGetAttribLocation(program, "position")
    GLES20.glEnableVertexAttribArray(position)
    GLES20.glVertexAttribPointer(position, 2, GLES20.GL_FLOAT, false, 0, quad)
    GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
  }

  private fun target(width: Int, height: Int): Target {
    val ids = IntArray(2)
    GLES20.glGenTextures(1, ids, 0); GLES20.glGenFramebuffers(1, ids, 1)
    GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, ids[0])
    GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, GLES20.GL_RGBA, width / 4, height, 0, GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, null)
    GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_NEAREST)
    GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_NEAREST)
    GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, ids[1])
    GLES20.glFramebufferTexture2D(GLES20.GL_FRAMEBUFFER, GLES20.GL_COLOR_ATTACHMENT0, GLES20.GL_TEXTURE_2D, ids[0], 0)
    check(GLES20.glCheckFramebufferStatus(GLES20.GL_FRAMEBUFFER) == GLES20.GL_FRAMEBUFFER_COMPLETE) { "Offscreen target incomplete" }
    return Target(width, height, ids[0], ids[1], ByteBuffer.allocateDirect(width * height))
  }

  private fun program(fragment: String): Int {
    fun shader(type: Int, source: String) = GLES20.glCreateShader(type).also {
      GLES20.glShaderSource(it, source); GLES20.glCompileShader(it)
      val ok = IntArray(1); GLES20.glGetShaderiv(it, GLES20.GL_COMPILE_STATUS, ok, 0)
      check(ok[0] != 0) { "Shader: ${GLES20.glGetShaderInfoLog(it)}" }
    }
    return GLES20.glCreateProgram().also {
      GLES20.glAttachShader(it, shader(GLES20.GL_VERTEX_SHADER, VERTEX))
      GLES20.glAttachShader(it, shader(GLES20.GL_FRAGMENT_SHADER, fragment))
      GLES20.glLinkProgram(it)
      val ok = IntArray(1); GLES20.glGetProgramiv(it, GLES20.GL_LINK_STATUS, ok, 0)
      check(ok[0] != 0) { "Program: ${GLES20.glGetProgramInfoLog(it)}" }
    }
  }

  override fun close() {
    if (closed) return
    closed = true
    texture.setOnFrameAvailableListener(null)
    surface.release(); texture.release()
    targets.values.forEach { GLES20.glDeleteFramebuffers(1, intArrayOf(it.framebuffer), 0); GLES20.glDeleteTextures(1, intArrayOf(it.texture), 0) }
    GLES20.glDeleteTextures(1, intArrayOf(oes), 0)
    GLES20.glDeleteProgram(lumaProgram); GLES20.glDeleteProgram(previewProgram)
    // Releasing the window surface disconnects the preview, so a normal session can use it again.
    EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT)
    EGL14.eglDestroySurface(display, window)
    EGL14.eglDestroyContext(display, context)
    EGL14.eglReleaseThread()
  }

  companion object {
    private val IDENTITY = floatArrayOf(1f, 0f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 0f, 1f)

    // The picture's box in texture coordinates (s0, t0, width, height) from the SurfaceTexture
    // transform: the transform maps the unit square onto it, whatever rotation or flip it adds.
    fun crop(m: FloatArray): FloatArray {
      val s = listOf(0f to 0f, 1f to 0f, 0f to 1f, 1f to 1f).map { (u, v) -> m[0] * u + m[4] * v + m[12] }
      val t = listOf(0f to 0f, 1f to 0f, 0f to 1f, 1f to 1f).map { (u, v) -> m[1] * u + m[5] * v + m[13] }
      return floatArrayOf(s.min(), t.min(), s.max() - s.min(), t.max() - t.min())
    }

    private const val VERTEX = """
      attribute vec2 position;
      uniform mat4 transform;
      varying vec2 uv;
      void main() {
        gl_Position = vec4(position, 0.0, 1.0);
        uv = (transform * vec4(position * 0.5 + 0.5, 0.0, 1.0)).xy;
      }"""

    // Output pixel (x, y) holds luma pixels 4x..4x+3 of row y, row 0 = the buffer's first row.
    // BT.601 weights undo the driver's YUV→RGB step closely enough for flash and QR reads.
    private const val LUMA = """
      #extension GL_OES_EGL_image_external : require
      precision highp float;
      uniform samplerExternalOES camera;
      uniform vec4 box;
      uniform vec2 size;
      float luma(float x) {
        vec2 st = box.xy + box.zw * vec2(x / size.x, gl_FragCoord.y / size.y);
        return dot(texture2D(camera, st).rgb, vec3(0.299, 0.587, 0.114));
      }
      void main() {
        float x = floor(gl_FragCoord.x) * 4.0 + 0.5;
        gl_FragColor = vec4(luma(x), luma(x + 1.0), luma(x + 2.0), luma(x + 3.0));
      }"""

    private const val PREVIEW = """
      #extension GL_OES_EGL_image_external : require
      precision mediump float;
      uniform samplerExternalOES camera;
      varying vec2 uv;
      void main() { gl_FragColor = texture2D(camera, uv); }"""
  }
}
