package io.leeo86.mxl.avsync

// Camera mode choice, pure so JVM tests cover it. Contract: the highest frame rate up to the cap
// (240 when automatic) at 1280×720 or more, 1920×1080 preferred at 120 fps and below.
object CaptureProfile {
  data class Size(val width: Int, val height: Int)
  // minFrameNs: the slower of the YUV and preview streams' minimum frame durations at this size.
  data class Output(val size: Size, val minFrameNs: Long)
  // A constrained high-speed size and the fps ranges listed for it.
  data class HighSpeed(val size: Size, val ranges: List<Pair<Int, Int>>)
  // highSpeed: a constrained high-speed session (GPU frames) instead of the normal one.
  data class Choice(val size: Size, val fps: Int, val aeMin: Int, val aeMax: Int, val highSpeed: Boolean = false)

  fun choose(aeRanges: List<Pair<Int, Int>>, outputs: List<Output>, requestedFps: Int): Choice? {
    val cap = if (requestedFps <= 0) 240 else requestedFps
    val usable = outputs.filter { it.size.width * it.size.height >= 1280 * 720 }
    // Fixed ranges first (constant frame interval), then the highest upper bound within the cap.
    val ranges = aeRanges.filter { it.second <= cap }.sortedWith(compareBy({ -it.second }, { it.second - it.first }))
    for ((low, high) in ranges) {
      // 1 % slack: some phones list 8 333 334 ns for 120 fps.
      val fits = usable.filter { it.minFrameNs <= 1_010_000_000L / high }
      val size = fits.minByOrNull { score(it.size, high) } ?: continue
      return Choice(size.size, high, low, high)
    }
    return null
  }

  // A high-speed mode only when it is faster than the normal choice (normalFps, 0 for none),
  // within the same cap and minimum size. Fixed ranges (fps, fps) only: a constant frame interval.
  fun chooseHighSpeed(configs: List<HighSpeed>, requestedFps: Int, normalFps: Int): Choice? {
    val cap = if (requestedFps <= 0) 240 else requestedFps
    return configs.filter { it.size.width * it.size.height >= 1280 * 720 }
      .flatMap { config -> config.ranges.filter { it.first == it.second && it.second in (normalFps + 1)..cap }.map { config.size to it.second } }
      .minWithOrNull(compareBy({ -it.second }, { score(it.first, it.second) }))
      ?.let { (size, fps) -> Choice(size, fps, fps, fps, highSpeed = true) }
  }

  private fun score(size: Size, fps: Int): Long {
    val wide = size.width * 9 == size.height * 16
    val preferred = if (fps <= 120) Size(1920, 1080) else Size(1280, 720)
    val exact = size == preferred
    // Exact preferred size, then 16:9, then the smallest picture above the minimum.
    return (if (exact) 0L else 1L shl 40) + (if (wide) 0L else 1L shl 39) + size.width.toLong() * size.height
  }

  // Exposure of the full frame interval; ISO scaled to keep the auto-exposure brightness.
  fun fixedExposure(autoExposureNs: Long, autoIso: Int, frameNs: Long, exposureRange: LongRange, isoRange: IntRange): Pair<Long, Int> {
    val exposure = frameNs.coerceIn(exposureRange)
    val iso = Math.round(autoIso.toDouble() * autoExposureNs / exposure).toInt().coerceIn(isoRange)
    return exposure to iso
  }
}
