# mxl-avsync 2.0.3

Fixes the audio correlation of the AVS2 signal. The signal, timing, ports,
metric names, NMOS resources and dependency pins are unchanged from 2.0.2.

## Fix

- **AVS2 correlation was 0.80 on a perfect link.** The AVS2 decoder finds the
  chirp at 8 kHz and took the correlation at the nearest whole sample. The
  generator's chirp sits half a sample off that 8 kHz grid, so a bit-exact MXL
  link always read 0.80. A path that resamples (for example x264 + AAC over
  SRT) moves the chirp to a random sub-sample position and read higher, up to
  about 0.98. So the value showed the sample phase, not the path quality. The
  2.0.1 note "about 0.80" for AVS2 was wrong about the reason: it is not by
  design.
- Now the correlation is the best match of the expected chirp over sub-sample
  shifts of the reference (±0.5 sample in 1/20 steps). The window is the 30 ms
  chirp only; the AVS2 timecode starts 10 ms later and is not in it. The timing
  estimate is unchanged.
- **One quality rule.** `good` needs a correlation of at least 0.85 and no
  clipping, for the analyzer, the phone and AVS2 packets (AVS2 packets were
  always marked `good` before).

## Numbers (file analysis, 20 s, 2 events/s, 36 events each, before → after)

| path | AVS2 1200 baud | legacy |
|---|---|---|
| bit-exact PCM | 0.796 → 1.000 | 0.994 → 1.000 |
| AAC 128 kbit/s | 0.976 → 1.000 (min 0.999) | 0.994 → 1.000 |
| Opus 128 kbit/s | 0.796 → 1.000 | 0.994 → 1.000 |
| AAC 64 kbit/s | 0.975 → 0.998 (min 0.995) | 0.993 → 0.998 |
| PCM + white noise | 0.793 → 0.993 | 0.987 → 0.993 |

The measured AV offsets are identical before and after in every case (for
example AAC 128k +0.3148 ms with a spread of 0.0075 ms; Opus −7.0075 ms, a
container delay that 2.0.2 shows too). In a unit test, all six 48 kHz phases
of the 8 kHz grid give at least 0.99 (2.0.2: 0.80 to 1.00), and stronger noise
lowers the value below 0.99.

## What changes for the platform

- `mxl_avsync_audio_correlation` and the event `correlation` are about 1.00
  on direct and mixer paths with AVS2, and a little lower on lossy paths.
  Alert thresholds near 0.9 work again.
- AVS2 events on a clean path report a smaller audio uncertainty (0.05 ms row
  instead of 0.25 ms), which the measured timing spread supports.
- No new env, metric or API field.
