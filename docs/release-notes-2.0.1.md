# mxl-avsync 2.0.1 (unreleased)

Fixes for the 2.0.0 deployment and operations notes for the AVS2 signal. The
signal, ports, NMOS resources and dependency pins are unchanged from 2.0.0.

## Fixes

- **Kernel clock on ptp4l + phc2sys hosts.** phc2sys clears `STA_UNSYNC` and
  keeps `maxerror` low but leaves `esterror` at the 16-s kernel default. 2.0.0
  therefore reported `unlocked` ("estimated error exceeds clock limit"). Now
  `esterror` at the default with `maxerror` at most 1 s is "error not reported
  (PTP discipline)" and `locked` with unknown accuracy. `maxerror` above 1 s
  (about 33 minutes without a discipline update, for example a stopped
  phc2sys) is `unlocked`.
- **Live event rate and audio timecode.** `AVSYNC_EVENT_RATE` and
  `AVSYNC_AUDIO_TIMECODE` set in the environment are start values; the Signal
  panel and `PATCH /api/v1/generator` change them on the running generator.
  2.0.0 answered `ok` and ignored the change. Settings shows such keys as
  "env start value, live-changeable".
- **Source picture in the live picture.** The uploaded PNG/GIF is drawn next to
  the Title in the MXL output, replaced by a new upload and restored after a
  container restart (not a new pod: `/config` is an emptyDir). Frames are scaled
  to at most 256 px at load; files above 16 MiB, 600 frames or 64 MiB decoded
  are refused with 413. `mxl_avsync_output_frames_missed_total{essence="video"}`
  now counts generator grains that were never written.

## What becomes visible on phc2sys hosts after the upgrade

With the default kernel clock source on a ptp4l + phc2sys node:

- `mxl_avsync_reference_sync{type="tai-ptp",state="locked"}` becomes 1 and the
  status reason reads `error not reported (PTP discipline)`.
- Live events start to carry latency fields (`video_latency_ms`,
  `audio_latency_ms`, `av_offset_arrival_ms`, index shifts). The measured AV
  offset and grade do not change.
- The audio latency is negative, about −25 ms on a direct MXL path, because the
  generator writes audio `AVSYNC_AUDIO_LEAD_MS` (default 20 ms) ahead of TAI.
  In the lab, video latency was about +12.6 ms.
- `mxl_avsync_clock_esterror_seconds` still shows 16 (the kernel value); the
  accuracy is unknown (`accuracy_ns` −1).

## Operations notes for AVS2

- **Audio correlation.** With the default AVS2 signal (`AVSYNC_AUDIO_TIMECODE=1200`
  or `600`) `mxl_avsync_audio_correlation` and the event `correlation` are about
  0.80 on a clean path. The legacy signal gives about 0.99. The value is the
  chirp match before the timecode, not a quality loss. Review alert thresholds
  set near 0.9 before switching to AVS2.
- **Drops and duplicates.** `mxl_avsync_video_drops_total` and
  `mxl_avsync_video_duplicates_total` are stubs: they have been 0 in every
  version. What changes with AVS2 is the event: there is no frame counter strip,
  so events carry `frame_counter_unavailable` instead of the per-event
  `video_drop` reason. A flash seen one frame late shows as a single event one
  frame off (for example −20 ms at 50p). Use transport or gateway diagnostics
  for frame drops.
- **Staggered upgrade.** A 1.x analyzer does not read AVS2: it reports grade
  `invalid` with no events. A 2.x analyzer reads both AVS2 and legacy. To
  upgrade generators and analyzers at different times, run the 2.x generators
  with `AVSYNC_AUDIO_TIMECODE=legacy` until every analyzer runs 2.x, then remove
  the setting. In legacy mode a deliberately set generator offset is not carried
  as an expected offset, so the grade uses the measured offset.
- **Live switch during a staggered upgrade.** Because the audio timecode is now
  live, the Signal panel can switch a legacy-started generator to AVS2. That
  stops every 1.x analyzer reading it.
- **Flow descriptions.** The IS-04 and MXL flow descriptions ("50/s, offset 0 ms",
  "2/s") are written when the flow is created and are not updated by a live
  rate or offset change; `/api/v1/generator` shows the current values.
