# mxl-avsync 2.0.7 (unreleased)

Steadier video latency on busy nodes, histograms that resolve the real paths,
and a fix to the audio "content vs grain" diagnostic. The signal, ports, NMOS
resources and dependency pins are unchanged from 2.0.6.

## Video arrival independent of the analyzer loop

Since 2.0.6, the platform's analyzers on one node showed video latency in two
modes: about 8.6 ms median (p95 14) and about 15.8 ms median (p95 31). The
analyzers switched between them at different times.

- **Cause.** The analyzer stamped a grain's arrival when its main loop got the
  grain. If the loop was busy at the writer's commit (QR or audio timecode
  decode, or catching up after a stall), the stamp came late by the rest of
  that work.
  - The loop's periodic work and the writer's commits both run on a 20 ms
    grid, so a phase between them can hold for minutes, then drift. That gives
    the two modes, separately per analyzer.
  - The analyzer loop was not changed in 2.0.6. The 2.0.6 generator change
    (the audio onset committed on time) moves when the analyzer's audio work
    happens. It may have set the phase, but this was not proven.
- **Fix.** A watch thread with its own reader polls the video flow head every
  0.5 ms, as for audio since 2.0.5. A grain's arrival is the first time the
  head covered it. If the loop was already waiting for that grain, its own
  time is used, which is then the same.
- **Lab** (analyzer on two CPUs with two busy loops each and two
  memory-bandwidth threads):

  | | before | after |
  |---|---|---|
  | video arrival vs timestamp, p50 / p95 | −16.4 / −11.4 ms | −18.6 / −18.0 ms |

  The generator commits at about −18.9 ms.
- **Expect on the platform:** direct video latency (absolute) about 1–2 ms
  plus the generator's commit lateness, steady on every analyzer.

## Finer histogram buckets

| Metric | Buckets | Series per essence |
|---|---|---|
| `latency_abs_seconds` | 0.5 ms; 1 ms steps to 10 ms; 5 ms steps to 60 ms; 80, 100, 150, 200, 250, 300 ms; 25 ms steps to 400 ms; 10 ms steps to 600 ms; 0.7, 0.8, 1, 1.5, 2, 3, 5, 10 s; +Inf | 62 |
| `av_offset_seconds` | 0 and ±0.5, 1, 2, 3, 5, 7, 10, 15, 20, 30, 40, 60, 100, 200, 300, 500 ms; +Inf | 36 (no essence) |

`histogram_quantile` estimates from synthetic events like the platform's
paths (3600 events each):

| Path | true p50 | 2.0.6 buckets | 2.0.7 buckets |
|---|---|---|---|
| SRT video, 495 ± 1.5 ms | 495.0 | 350.0 | 495.0 |
| mixer audio, 42.7 ± 2 ms | 42.7 | 35.0 | 42.6 |
| direct video, 8.6 ± 1 ms | 8.6 | 7.7 | 8.6 |

Every `le` value of 2.0.6 is still present, so existing queries keep working.

## "Video a frame off" on the SRT loop

- **There is no analyzer flag with that meaning.** AVS2 events carry only
  `qr_flash` and `frame_counter_unavailable`; the latter says AVS2 has no frame
  counter strip, on every path.
- **What the SRT loop really shows** is a constant re-stamp, reproduced in the
  lab with a relay that maps video 480 ms (24 grains) and audio 476.5 ms later:
  - `video_index_shift_ms` +480 and `video_content_vs_grain_ms` −480;
  - audio +476.5;
  - AV offset −3.5 ms, graded pass.
  - The AV offset and the absolute latency are right.
- **The analyzer bug was the audio side of the diagnostic.**
  `audio_content_vs_grain_ms` used the detected time, which is on the grain
  timeline, so it was always about 0. `content_vs_grain_av_ms` therefore
  showed the whole video re-stamp (−480 ms) as an AV content difference.
  Now:
  - `audio_content_vs_grain_ms` uses the AVS2 packet's send time (legacy: the
    video set time plus the expected offset);
  - behind the relay, video −480 / audio −476.5 ms, and `content_vs_grain_av_ms`
    is −3.5 ms, the real AV difference.
- **Not changed:** `*_content_vs_grain_vs_index_shift_ms` subtracts the index
  shift from content vs grain, so a re-stamp counts twice (−960 ms here).
  Content vs grain is the negative of the index shift by definition, so their
  sum (0 when consistent) would be the useful check. This is left as it is,
  for a decision.
