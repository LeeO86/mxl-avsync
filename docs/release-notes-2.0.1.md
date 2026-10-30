# mxl-avsync 2.0.1 (unreleased)

Fixes for the 2.0.0 deployment and operations notes for the AVS2 signal. The
signal, API, ports, NMOS resources and dependency pins are unchanged from 2.0.0.

## Operations notes for AVS2

- **Audio correlation.** With the default AVS2 signal (`AVSYNC_AUDIO_TIMECODE=1200`
  or `600`) `mxl_avsync_audio_correlation` and the event `correlation` are about
  0.80 on a clean path. The legacy signal gives about 0.99. The value is the
  chirp match before the timecode, not a quality loss. Review alert thresholds
  set near 0.9 before switching to AVS2.
- **Drops and duplicates.** AVS2 has no per-frame counter strip. Events carry the
  flag `frame_counter_unavailable`, and `mxl_avsync_video_drops_total` and
  `mxl_avsync_video_duplicates_total` stay 0. Use transport or gateway
  diagnostics for frame drops. A frame lost on the analyzer side shows as a
  single event one frame off (for example −20 ms at 50p), not as a drop.
- **Staggered upgrade.** A 1.x analyzer does not read AVS2: it reports grade
  `invalid` with no events. A 2.x analyzer reads both AVS2 and legacy. To
  upgrade generators and analyzers at different times, run the 2.x generators
  with `AVSYNC_AUDIO_TIMECODE=legacy` until every analyzer runs 2.x, then remove
  the setting. In legacy mode a deliberately set generator offset is not carried
  as an expected offset, so the grade uses the measured offset.
