# Changelog

## 2.0.4 (unreleased)

- Generator render cost: the AVS2 picture is no longer drawn and packed in full on every grain. Bars, icon and titles are drawn once per settings change; each grain redraws and repacks only what changed (sync scale, flash edges, QR and event line per event, GIF frames), byte-identical to a full render. 1080p50: 7.9 → 0.84 ms per grain (p99 10.8 → 3.7 ms); the v210 packer is also faster (2.5 → 1.6 ms for a full frame). Lab, node-like load (two bursty busy loops per generator CPU plus four memory-bandwidth threads, 5 min): 2.0.3 missed 805 grains/min (27 %) and committed p50 +37 / p99 +118 ms after `T_k` at 0.48 core; 2.0.4 missed 0, commit p50 −16.8 / p99 −7.2 ms (before `T_k`), 0.14 core.
- New `mxl_avsync_generator_render_seconds` histogram (render + pack per video grain).
- Analyzer reads every input grain in order and catches up after a stall, instead of jumping to the newest grain whenever it was one behind. Jumping skipped the grain with an event's first flash frame under CPU load and gave events one frame off (lab node-like load: 34 of 602 → 0 of 607). New `mxl_avsync_input_grains_skipped_total{essence="video"}` counts grains not analysed (written invalid by a source that missed them, overwritten, or a stall longer than half the flow history).
- `mxl_avsync_events_total{result}` is a real counter: every closed event is counted once, under `paired` or its reason. Before, it counted the last 1000 retained events, so it stopped growing after about 8 minutes (and `AVSyncNoEvents`, `rate(...paired[1m]) == 0`, could fire on a healthy analyzer). New results `quality` and `other`. An audio event that no video event claims within its window is now reported as `missing_video` (it was dropped silently); with a 2.0.3 generator missing 27 % of grains, 14 of 361 events were lost this way in 3 min and are now counted.
- Live analyzer pairing keeps only open events and recent observations; before, its history grew for the life of the process.

## 2.0.3 (2026-10-30)

- Audio correlation is the best sub-sample match of the expected chirp. The AVS2 decoder correlates at 8 kHz, where the generator's chirp sits half a sample off the grid, and took the value at the integer lag: a bit-exact MXL path read 0.80, while a resampling path (x264 + AAC over SRT) read up to about 0.98. It is now the maximum over sub-sample shifts of the reference (±0.5 sample, 1/20 steps); the timecode is not in the 30 ms window. Bit-exact AVS2 0.796 → 1.000, AAC 128k 0.976 → 1.000, Opus 128k 0.796 → 1.000, legacy 0.994 → 1.000; added noise lowers it. Timing is unchanged (identical offsets in every case).
- One quality rule: `good` needs a correlation of at least 0.85 and no clipping, for detector hits (analyzer, phone) and AVS2 packets alike (AVS2 packets were always `good`). AVS2 events on a clean path move from the 0.25 ms to the 0.05 ms audio uncertainty row.
- The 2.0.1 operations note "about 0.80" for AVS2 is corrected: it was this measurement error, not a property of the signal. Alert thresholds near 0.9 work for AVS2 again.

## 2.0.2 (2026-10-30)

- The live generator no longer drops video grains under CPU load. It wrote grain `k` only after `T_k` (render, then commit about one render time late), took the grain index from `mxlGetCurrentIndex`, which rounds to the nearest grain, and so skipped a whole grain whenever the loop woke more than half a grain late. A skipped event grain showed as a single event one frame off (−20 ms at 50p, `video_content_vs_grain_ms` −20). Now grain `k` is rendered right after grain `k − 1` is committed and committed at `T_k − AVSYNC_VIDEO_LEAD_MS` (new, default 20 ms, restart, 0–100); a late grain is still written, and grains are skipped only when the writer is more than four grains behind. Lab, generator and analyzer sharing two CPUs with a bursty load (5 min): missed grains 44/min → 0, events one frame off 10 of 605 → 0 of 605, video commit vs `T_k` median +13.5 ms → −18.6 ms.
- Audio is written by its own thread (as SPECIFICATION §2.2 describes) every 10 ms block, between `AVSYNC_AUDIO_LEAD_MS` and that lead plus one block ahead of its timestamp. Before, it was written once per video grain after the video render, so a slow render also shortened the audio lead.
- `mxl_avsync_output_frames_late_total{essence="video"}` now counts generator grains committed after their own timestamp (before: a loop that woke more than one grain late). With the default lead it stays 0 unless the generator falls more than 20 ms behind.
- Presentation of latency: the UI and `/api/v1/measurement` lead with the index-based offset, labelled "AV sync (what MXL consumers see)" (new `av_sync_ms`); the per-essence latency is labelled "arrival vs timestamp" (new `video_arrival_vs_timestamp_ms` / `audio_arrival_vs_timestamp_ms`, same values as `video_latency_ms` / `audio_latency_ms`) with a note that directly behind the generator it shows the generator's own write timing. Existing fields keep their names and values.
- The generator publishes its emit offsets (commit time − content timestamp): `/api/v1/status` `emit_offsets` (video −`AVSYNC_VIDEO_LEAD_MS`, audio −(`AVSYNC_AUDIO_LEAD_MS` + 5 ms), plus the measured video commit time p50/p99/max), and the NMOS flow tag `urn:x-mxl:tag:avsync-emit-offset-ms/v1.0` on its video and audio flows. An analyzer with a Query API (`NMOS_QUERY_ADDRESS` or the registry address) reads the tag of the flows it reads and adds `video_chain_latency_ms` / `audio_chain_latency_ms` (and the `*_source_emit_offset_ms` used) to events and the measurement; its status shows `source_emit_offsets`. Behind a gateway (no tag) chain latency stays absent.
- `AVSYNC_PTP_STATUS_URL`: optional `grandmaster`, `port_state`, `ptp4l_offset_ns`, `phc2sys_offset_ns` and `path_delay_ns` in the page fill `gm_present`, `gm_identity`, `port_state` and `master_offset_ns` and show the offsets and path delay in the status. The two-key page still works.

## 2.0.1 (2026-10-30)

- `AVSYNC_EVENT_RATE` and `AVSYNC_AUDIO_TIMECODE` are live settings: when set in the environment they are start values, and the Signal API/UI change them on the running generator (before, the change was silently ignored with `ok:true`).
- Kernel clock: `esterror` at the 16-s kernel default is "not reported" even when `maxerror` is low, so ptp4l + phc2sys hosts (esterror 16 s, maxerror 0) lock with unknown accuracy instead of "estimated error exceeds clock limit". `maxerror` above 1 s (about 33 min without a discipline update, e.g. phc2sys stopped) unlocks.
- The live MXL generator draws the uploaded source picture next to the Title, replaced by a new upload and restored after a container restart (not a new pod: `/config` is an emptyDir); before, only file output showed it. Pictures are decoded once, outside the lock the generator uses, and scaled at load to at most 256 px per side; uploads above 16 MiB, 600 frames or 64 MiB decoded are refused with 413, a failed upload keeps the previous picture, and a broken stored picture is skipped with a `source_image_skipped` warning. `mxl_avsync_output_frames_missed_total{essence="video"}` now counts generator grains never written (it was always 0).
- Operations (docs only): with AVS2, `mxl_avsync_audio_correlation` is about 0.80 (legacy about 0.99), so review alert thresholds near 0.9; AVS2 events no longer carry the per-event `video_drop` reason (no frame counter strip), while `mxl_avsync_video_drops_total` / `mxl_avsync_video_duplicates_total` are stubs that were always 0 in every version; for a staggered upgrade run 2.0 generators with `AVSYNC_AUDIO_TIMECODE=legacy` until every analyzer runs 2.0. SPECIFICATION no longer claims that `config.json` is written.

## 2.0.0 (2026-10-29)

- Introduce the shared AVS2 QR + flash picture in file, MXL and iPhone sources
  and receivers. Decode full event identity independently of flash timing;
  retain legacy input compatibility. Upgrade compact-signal receivers together.
- Replace per-frame full-picture rectification on iOS with sparse camera-plane
  photometry. Support crops retaining QR quiet zone and flash field, validate
  pulse-time QR evidence, and preserve onset through delayed QR pixel settling.
- Center the app icon above AVsync, use Title for the existing saved source
  label, remove the obsolete extra Title control and render a smaller subtitle.
- Draw a black centerline on the moving sync box and keep white scale ticks
  above it, using the same foreground in native iPhone and server rendering.
- Refresh public project-site screenshots and release installation guidance.


- Keep the last valid mobile measurement saveable through temporary signal loss,
  label its age and spread, and preserve acquisition time in saved/exported results.
  Source changes and explicit clears still discard the retained snapshot.
- Stabilize the camera outline with bounded hold/fade and corner smoothing;
  reject one-off geometry jumps and expire lost targets within 600 ms. The
  detector's timing, validity gates and AV samples remain unchanged.

- Fix source-picture selection so Apply enables and uploads PNG/GIF files; retain
  failed or newly selected uploads and reset the file input on Apply/Discard.
- Show only role-appropriate previews and replace unavailable images with a
  recoverable placeholder. Generator pages no longer request analyzer input.
- Move generator activity from the red banner to a green header status pill,
  with neutral muted/stopped states. Error banners remain for actual failures.


- Brand the mobile app AVsync, enlarge the header mark and generate separate
  iOS launcher, framed content and Android adaptive/themed icons from one SVG.
- Show the applied calibration and saved date in mobile settings; distinguish
  inactive corrections from 0 ms and dismiss the sheet by tapping its backdrop.

- Share the server color-bar/sync-scale renderer with the iPhone generator.
  Add saved source name/subtitle and a private system-picker image import.

- Add a native iPhone fullscreen generator (1200 baud, 1/2 Hz) and automatic
  optical/tile search in the live analyzer. Preserve source-frame timing across
  camera frame rates and explicitly mark phone source latency as estimated.

- Integrate compact audio-only timecode across the generator, file/live analyzers
  and native iOS app, with CRC/FEC, full event identity and legacy compatibility.
- Align each white pulse to chirp onset and add draft-preserving 1/2-Hz and
  1200/600-baud controls. Count skipped live collisions; reject overlapping files.
- Decode audio without picture/QR and show separately estimated phone latency,
  independent signal pills, picture outline and current-stage diagnostics.
- Restore working native capture/calibration, retain zero correction without
  calibration, and preserve antialiased generator text. Add end-to-end channel,
  file, native iOS and form regressions; hardware timing remains unverified.

## 1.0.1 (2026-10-26)

- Fixed a pre-existing `--locate` failure on scaled, padded full-renderer signals: QR/text squares could displace real corners. Spatial candidate retention, local ring refinement, strip/geometry checks and stable 640×360 rectification now recover the full picture. Regression coverage checks actual corner coordinates and multiple marker sequences/states.

- NMOS node labels equal `NMOS_LABEL` exactly, with the role on the device label; sender and receiver labels are unchanged.
- Clock source selection: explicit URL override, optional read-only ptp4l management queries, then kernel fallback, with `sync_source`, reasons and PTP grandmaster/offset diagnostics in status, UI and metrics. Unreported kernel PTP error defaults no longer prevent lock; NTP still requires a configured TAI offset and reported error within the limit.
- Web Signal and Settings forms preserve local edits during periodic status/preview refreshes. Apply/Discard and Unsaved changes are explicit; failed saves and edits made during an in-flight save are retained.

- Readiness no longer waits for analyzer routes, input flows or signal, preventing a deployment deadlock when controllers route only ready pods. `/readyz` checks process/HTTP health and configured NMOS Query API registration; the generator also waits for output-flow creation. Media state remains visible through `/statusz`, metrics and the UI. Regression coverage includes startup before routing, unroute/reroute, signal loss and registry disappearance.

## 1.0.0 (2026-10-25)

- Clock check without an external URL: `sync_state` comes from the kernel clock (`adjtimex(2)`, read-only, no privilege). `locked` needs STA_UNSYNC clear, a TAI offset and `esterror` ≤ `AVSYNC_CLOCK_MAX_ERROR_US` (default 100). It cannot tell PTP from NTP. `AVSYNC_PTP_STATUS_URL` stays as an override. New gauges `mxl_avsync_clock_synchronized`, `mxl_avsync_clock_esterror_seconds`, `mxl_avsync_clock_tai_offset_seconds`; `/api/v1/status` `ptp` has `clock_synchronized` and `tai_offset_s`.
- Web UI: the Measurement tab shows every measurement value as labelled cards next to the result picture. Events show UTC ISO time and offsets at 0.1 ms. The header, tabs, cards and colours follow the sibling functions, with a light theme. `/api/v1/status` has `video_rate_fps`.
- The generator's audio flow has a proper description, like the video flow.

- Event model, chirp synthesis and matched-filter detection, marker and Gray strip, pairing, grading and statistics in `avsync-core`.
- `mxl-avsync generate`, `analyze`, `validate` and `merge`. Default file is MOV + ProRes 422 HQ + PCM 24-bit. Container time bases are checked.
- `mxl-avsync serve` with REST, widgets, `/livez`, `/readyz`, `/metrics` and an IS-04/IS-05 node for `AVSYNC_ROLE=generator|analyzer`.
- Clean PCM timing error is ~0 ms (target ≤ 0.1 ms). SNR 10 dB and the G.711 table-codec test aid are 100 % detection on the unit rows in `docs/test-matrix.md`.
- Live MXL video (`video/v210`) and audio (`audio/float32`) reader and writer in `mxlio/`, linked only into `mxl-avsync`. Interlaced flows are fields. `/readyz` is 200 when those flows are open. Closed loop on one host: set offsets 35, 0 and −35 ms came back as 35.0, 0.0 and −35.0 ms.
- Result picture flow `uuid_v5("mxl-avsync/<seed>/flow/result/<format>")` (default 960×540). About 1 fps JPEG previews at `/api/v1/preview/input.jpg` and `/api/v1/preview/generated.jpg` (404 `no frame yet` until a frame exists). No preview stream.
- With `NMOS_REGISTRY_ADDRESS` set, `nmos_registered` and `mxl_avsync_nmos_registry_up` follow the Query API (`GET /x-nmos/query/v1.3/nodes/<id>` on `NMOS_QUERY_ADDRESS` or the registry host, port `NMOS_QUERY_PORT`). `/readyz` stays 503 until that node is listed. `mxl_avsync_nmos_activations_total` counts IS-05 activations. The header pill reads `registered`, `not registered` or `off`.
- NMOS node is nmos-cpp `fe303849` when enabled. Ids stay `mxl-avsync/<seed>/...`. DNS-SD is off by default. IS-05 activation opens the analyzer's readers. A taken NMOS port exits 75. The ST 2110 hour run is not a feature of this repo; it goes through mxl-st2110-gateway.
- `AVSYNC_PTP_STATUS_URL` is polled for `{"sync_state","accuracy_ns"}`. Only `http://` is accepted. A missing URL or a failed read leaves `sync_state` `unknown`. When the clock is `locked` and the QR names the event, live results include `video_latency_ms`, `audio_latency_ms`, the index shifts and `av_offset_arrival_ms`.
- `mxl-avsync paths` lists `sibling-gateways` instead of `srt-live`, `g711-sip`, `st2110-loop` and `fabrics-cross-node`.
- Live results include `video_content_vs_grain_ms`, `audio_content_vs_grain_ms` and `content_vs_grain_av_ms` from the QR or chirp time and `mxlGrainInfo.index`. Arrival comparisons are added only when the PTP clock is locked.
- An IS-05 `master_enable: false`, or a change of domain or flow id, closes the analyzer readers on the next loop. `input_state` becomes `not_routed` (or `waiting` when the new flow is not in the scan path yet) and `/readyz` is not ready. Paired events stop until the new route is open.
- `PATCH /api/v1/generator` and `PUT /api/v1/config` change a live key such as `AVSYNC_AV_OFFSET_MS` on a running process, including when that variable was set in the environment at start. The next events use the new offset. Import still skips env-set keys.
- The current grade, the grade metric and the result picture become `invalid` after `AVSYNC_STALE_S` without a new valid event. The measurement then names the age and does not repeat the in-sync sentence.
- The offset histogram, `mxl_avsync_audio_correlation`, `mxl_avsync_audio_level_dbfs` and the analyzer's `mxl_avsync_output_frames_total{essence="video"}` follow the live events and the result writer.
- `GET /api/v1/events` returns the newest `limit` records, newest first, and a `total` count. `since` still drops older sequence numbers.
- The result picture draws the signed offset at about a third of the picture height, the sentence under it, a 60 s trend in the bottom third, the input state, the age, and the full-picture states NO SIGNAL, WAITING, NOT ROUTED and NO EVENTS.
- The node label includes the role and is not the device label. Senders, flows and the device carry `urn:x-mxl:tag:test-signal/v1.0`. Video flow descriptions include the rate and the offset. Receivers publish BCP-004-01 constraint sets. `interface_bindings` names the host interface. Node `hostname` has no domain suffix.
- Settings groups the keys from `GET /api/v1/config`, applies them with `PUT`, shows validation errors and `restart_required`, keeps env-set keys read-only, and exports and imports the document. Grading windows are in that form.
- The `avsync` widget shows the signed offset, the sentence and the grade colour, the latency only when a measurement has one, and a trend from the statistics series. Grey is the invalid state. The `generator` widget shows TEST SIGNAL ACTIVE, the set offset, and mute and stop controls.
- The result picture keeps text inside a 5% margin, uses a luma step of at least 300 against the grade colour, names the latency or `reference: unknown`, the routed sender ids, a timestamp and the event count.
- Tabs take their address from the URL hash (`#Input`, `#Events`, `#NMOS`, `#Status`, and the role's other tabs).
- The web page has Input, NMOS, Status and Events tabs, and header pills for role, label, version, NMOS (`registered`, `not registered` or `off`), health and input state. `/api/v1/status` includes `nmos_enable` and `nmos_port`.
- A generator that finds a leftover domain with a different id names `MXL_OUTPUT_DOMAIN_ID` and `NMOS_SEED` in the status detail and fails `/livez`. It does not delete that directory.
- Demo Compose file and Kubernetes pod run both roles on a shared memory volume with NMOS and DNS-SD off. No registry.
- QR decode is zxing-cpp 2.3.0, the decoder named in the spec. Encode stays Nayuki. The earlier quirc decoder is gone.
- Metrics are exported from process start, at 0 until something happens. The names are `mxl_avsync_info`, `mxl_avsync_generator_active`, `mxl_avsync_generator_audio_muted`, `mxl_avsync_generator_events_total`, `mxl_avsync_output_frames_total`, `mxl_avsync_output_frames_late_total`, `mxl_avsync_output_frames_missed_total`, `mxl_avsync_input_state`, `mxl_avsync_input_resyncs_total`, `mxl_avsync_events_total`, `mxl_avsync_grade`, `mxl_avsync_av_offset_ms`, `mxl_avsync_av_offset_deviation_ms`, `mxl_avsync_av_offset_seconds`, `mxl_avsync_video_drops_total`, `mxl_avsync_video_duplicates_total`, `mxl_avsync_audio_correlation`, `mxl_avsync_audio_level_dbfs`, `mxl_avsync_reference_sync`, `mxl_avsync_nmos_registry_up`, `mxl_avsync_nmos_activations_total`. `mxl_avsync_video_latency_ms` and `mxl_avsync_audio_latency_ms` are not exported.
- The image `HEALTHCHECK` calls `/livez` on `WEB_PORT` (default 8180).
- The Events tab shows a TAI timestamp instead of the raw nanosecond count, offsets to 0.1 ms, and correlation to three decimal places.
- An IS-05 receiver activation needs `transport_params` with `mxl_domain_id` and `mxl_flow_id`. `sender_id` alone does not route the analyzer.
- Image `ghcr.io/leeo86/mxl-avsync`. The runtime user is 1000:1000. The build stage runs the unit tests. `cap_sys_nice` is not set. `v1.0.0` is not tagged yet. `deploy/k8s/deployment.yaml` is one Deployment per role and was not applied. AMWA IS-04-01, IS-04-02, IS-05-01 and IS-05-02 run in CI via `tests/nmos/amwa.sh`; that job was not executed on the machine that added it.
- SIGTERM removes this node's resources from the registry before the process exits. The IS-04-02 suite is aimed at the registry registration and query ports. The CI registry priority is 0. AMWA results of Not Applicable, Test Disabled, Could Not Test, Manual and Not Implemented are accepted.

## 1.1 / 2.0

- Fiducial search for a scaled or cropped picture, `analyze --locate`.
- File-mode H.264 bitrate rows and G.711 PCMU/PCMA at 8 kHz, as test aids only (FFmpeg). Not live SRT or SIP inputs.
- Cross-node merge view (`merge`, `POST /api/v1/merge`).
- Phone measurement core (rolling shutter, calibration) and the Expo app sources under `mobile-app/`.
