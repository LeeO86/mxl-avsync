# Changelog

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
