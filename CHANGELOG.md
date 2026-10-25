# Changelog

## 1.0.0

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
- A generator that finds a leftover domain with a different id names `MXL_OUTPUT_DOMAIN_ID` and `NMOS_SEED` in the status detail and fails `/livez`. It does not delete that directory.
- Demo Compose file and Kubernetes pod run both roles on a shared memory volume with NMOS and DNS-SD off. No registry.
- QR decode is zxing-cpp 2.3.0, the decoder named in the spec. Encode stays Nayuki. The earlier quirc decoder is gone.
- Metrics are exported from process start, at 0 until something happens. The names are `mxl_avsync_info`, `mxl_avsync_generator_active`, `mxl_avsync_generator_audio_muted`, `mxl_avsync_generator_events_total`, `mxl_avsync_output_frames_total`, `mxl_avsync_output_frames_late_total`, `mxl_avsync_output_frames_missed_total`, `mxl_avsync_input_state`, `mxl_avsync_input_resyncs_total`, `mxl_avsync_events_total`, `mxl_avsync_grade`, `mxl_avsync_av_offset_ms`, `mxl_avsync_av_offset_deviation_ms`, `mxl_avsync_av_offset_seconds`, `mxl_avsync_video_drops_total`, `mxl_avsync_video_duplicates_total`, `mxl_avsync_audio_correlation`, `mxl_avsync_audio_level_dbfs`, `mxl_avsync_reference_sync`, `mxl_avsync_nmos_registry_up`, `mxl_avsync_nmos_activations_total`. `mxl_avsync_video_latency_ms` and `mxl_avsync_audio_latency_ms` are not exported.
- Image `ghcr.io/leeo86/mxl-avsync`. The runtime user is 1000:1000. The build stage runs the unit tests. `cap_sys_nice` is not set.

## 1.1 / 2.0

- Fiducial search for a scaled or cropped picture, `analyze --locate`.
- File-mode H.264 bitrate rows and G.711 PCMU/PCMA at 8 kHz, as test aids only (FFmpeg). Not live SRT or SIP inputs.
- Cross-node merge view (`merge`, `POST /api/v1/merge`).
- Phone measurement core (rolling shutter, calibration) and the Expo app sources under `mobile-app/`.
