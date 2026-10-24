# mxl-avsync — Implementation plan

This plan comes before any code. It is updated as the work goes on: every decision and every deviation from `SPECIFICATION.md` is recorded here, as in the sibling repos.

## Status (2026-10-24)

Core, file tools and the serve control plane are implemented and tested on the cloud VM. Live MXL I/O is not linked: `/readyz` is 503 until a build with libmxl creates flows. The ST 2110 hour runs and the phone app are unchanged host / later work. Decisions of §18 still hold. Deviations are numbered below.

## Pins

| Component | Pin |
| --- | --- |
| MXL | `dmf-mxl/mxl` `release/v1.1` at `218ddaa0a08c12ffe75fc475ae65aa3d9eef16d7`, `-DMXL_ENABLE_FABRICS_OFI=OFF`; one `MXL_REF` in `docker/Dockerfile` and `.github/workflows/ci.yaml` |
| nmos-cpp | `fe303849527394b03bdedc8f161f377fe458bb62` (the siblings' commit) |
| Blend2D, DejaVu Sans | the versions mxl-multiviewer uses |
| FFmpeg | Ubuntu 24.04 system libraries (file mode, transcode tests) |
| zxing-cpp, Nayuki QR, pocketfft, doctest | vendored, versions recorded in M0 |

## Rules for every milestone

The definition of done of the draft (§15 there): code, tests that ran with their real results, updated documentation (README, SPECIFICATION where it changed, this plan), known limits, and the next concrete step. No accuracy is claimed that a test has not shown. Each milestone ends with a short summary in this file: what was built, changed files, tests and results, limits, next step.

## Milestones

The order follows the draft's phases, merged into the siblings' milestone pattern. The phone app is last (M-P0 to M-P2) and starts only after 1.0.

### M0: event model and audio core (draft phases 0–2)

Goal: the measuring core without any media library, with real numbers.

- Repository scaffold, CMake, CI (core only at first), doctest, `AGENTS.md`.
- `schemas/`: QR payload, manifest, result, event log (JSON Schema 2020-12) and their validation tests.
- `avsync-core`: event plan (grain grid at 25/50/59.94p and 1080i25, offsets, sweep, determinism), sign convention, grading, statistics, uncertainty model.
- Chirp synthesis (four shapes, sub-sample placement, sample-index purity).
- Matched-filter detector (pocketfft, parabolic interpolation, chirp id, quality, missing/duplicate/ambiguous).
- The robustness suite of SPECIFICATION §14.1 for audio (level, noise, AGC, band-pass, resampling, G.711 μ-law/A-law, gaps, cross-confusion) with a results table: timing error p50/p99 and detection rate per condition.
- Decide: chirp shapes, default threshold, the uncertainty table. Record them here.

Exit: the audio detector meets ≤ 0.1 ms p99 timing error on the clean PCM path and ≥ 99 % detection at SNR 10 dB and through G.711; or this plan records what it reaches and the spec is adjusted before M1.

### M1: test picture and the file tools (draft phases 3–4: the draft's MVP)

- Picture composition with Blend2D: static layer, timing marker, Gray strip, sweep bar, counters, QR (Nayuki), TEST SIGNAL, fiducials; v210 and 8-bit RGB for files.
- Marker detector on luma (thresholds from the reference blocks, Gray decode, parity, drops and duplicates, quality) and its synthetic-frame tests.
- QR decode with zxing-cpp.
- `mxl-avsync generate`: MOV + ProRes 422 HQ + PCM 24-bit by default, MP4/MKV/AAC/H.264 variants; manifest; the container check (SPECIFICATION §9.3).
- `mxl-avsync analyze`, `validate`, `merge`: decode with FFmpeg keeping PTS, pairing, grading, statistics, human and JSON reports, `--fail-on`.
- `tests/integration/file.sh`: offsets 0, +35, −35, a sweep, inserted delays, and the file-mode transcode set of SPECIFICATION §13 (H.264 bitrates, scaling, frame-rate conversion, resampling, level, AGC, G.711). Results into `docs/test-matrix.md`.
- Decide the container default from the round-trip results; record it here.
- First container image (CLI only) on `main` (`nightly-dev`).

Exit: the MVP criteria 1–9 of the draft (§13 there) hold for files; criterion 10 (no routing) is trivially true here and is proven for live in M2.

### M2: live generator on MXL

- Config (env > file > defaults, exit 78), state directory, exit codes, roles, `serve`.
- MXL: own output domain (G2), video writer (TAI paced, static layer copy + changing parts), audio writer (blocks ahead of TAI, pure sample function), flow tokens on format change.
- NMOS node with the two senders, labels, test-signal tag and description, UUIDv5 ids.
- REST for the generator, mute and stop/start, `TEST SIGNAL ACTIVE`; `/livez`, `/readyz`, `/metrics` (all families at 0 from the start).
- Measure: drawing cost per grain and CPU at 1080p50 and 2160p50.
- `tests/integration/live.sh` part 1: flows exist, events at the right indices (read back with a test reader), mute, stop/start, SIGTERM.

Exit: a routed generator shows the test picture on a multiviewer tile and the chirps on a monitor in the lab; the indices of the marker changes and chirp centres match `T_k` exactly in the test reader.

### M3: live analyzer on MXL

- Video and audio readers: no grain copy, regions only, torn-read check, arrival stamping, batch-commit detection, recreated-flow reopen with a fresh MXL instance, grain size check, interlaced (field) handling, `waiting`/`no_signal`/`format_mismatch`.
- QR worker, pairer (ids, ±2 periods, `out_of_range`), index and arrival times, latencies and index shifts, grading (median of N, stale), statistics windows, event log with rotation.
- NMOS receivers (IS-05 with BCP-007-03), route persistence and restore.
- REST for measurement, events, statistics, grading; WebSocket.
- Test tool `tests/tools/avsync-shift`: an MXL relay that delays audio or video by n ms or drops/duplicates frames, for the controlled-delay rows of the matrix.
- `tests/integration/live.sh` part 2: offsets 0/+40/+80 and their grades, +20 ms through the shift tool, generator restart → reopen, mute → invalid, restored routes.
- Measure: reader wake-up resolution (the arrival-time part of the uncertainty) and CPU per analyzer.

Exit: in the lab the MXL reference path measures 0 ms ± the M0 audio resolution for 1 h at 1080p50 and 1080i25, with latency figures and their uncertainty.

### M4: operator outputs

- Result video flow (960×540 at 25 Hz, redraw 5 Hz, TAI-paced writer, state texts).
- Web UI (Vue 3, family look): generator and analyzer tabs of SPECIFICATION §16.1.
- Widgets `avsync` and `generator` with the B.2 contract (CORS, CSP, messages, themes).
- Config export and import, `KEY=value` export.
- `deploy/grafana/mxl-avsync.json` and the alert proposal (SPECIFICATION §16.6) in `deploy/prometheus/avsync-rules.yaml`.

Exit: an operator sees the grade in the widget on an operator screen and on a multiviewer tile; a forced +80 ms offset turns both red and fires the alert in a local Prometheus.

### M5: ST 2110 path, hardening, release 1.0.0

- ST 2110 loop through mxl-st2110-gateway in the lab, 1080p50 and 1080i25, 1 h each: AV offset, index shift and latency per essence; the result into `docs/test-matrix.md`, with the gateway version.
- Performance of both roles on both host classes (8-core AVX2 desktop CPU; Skylake-SP server) into `docs/performance.md`; catalog resources from it.
- AMWA IS-04-01, IS-05-01, IS-05-02, BCP-007-03-01 (`tests/nmos/amwa.sh`).
- Container image with OCI and MXL labels, tags, Compose demo (registry stand-in, generator, analyzer, a monitor), `deploy/k8s/` for both roles.
- README (settings, ports, exit codes, API, widgets, CLI, platform notes), CHANGELOG, NOTICE (licences checked).
- Platform hand-off as a note to the platform maintainers (they own that repo): the two catalog types, the chart type, the dashboard rows, the alert rules, the measured resources (SPECIFICATION §17).
- Release 1.0.0 with the platform release notes (Appendix B.3), metric names listed.

Exit: 1.0.0 released; a test production with one generator and two analyzers comes up and down cleanly on the platform.

## After 1.0

| Release | Work |
| --- | --- |
| 1.1 | SRT + H.264 path (mxl-srt-gateway, several bitrates incl. low); cross-node through the fabrics agent (two analyzers, `merge` live view); G.711 live once a SIP function exists; fiducial marker search for a scaled or cropped picture (a multiviewer tile) — the same search the phone app needs |
| 2.0 | the phone app (M-P0 to M-P2 below) |
| later | per-channel sequences (channel-swap test), lip-sync on programme material, IS-07 events |

### M-P0: phone platform spike (after 1.1's fiducial search)

- Expo dev build with one native module per platform; `avsync-core` compiled in (iOS Objective-C++, Android NDK/JNI).
- Measure on at least two iOS and three Android models: can video frames and audio input be timestamped on one clock (iOS host time; Android `REALTIME` sensor timestamps and AAudio/Oboe), at what frame rate, with what jitter.
- Decide: Expo with native modules stays, or native apps (the draft's rule: precise capture timestamps over code reuse). Record it here.

Exit: a documented answer per device class, and the decision.

### M-P1: calibration rig and method

- The LED + piezo reference device (microcontroller, parts list, firmware in `mobile-app/calibration/`), its own timing checked with an oscilloscope.
- Calibration procedure in the app: measure the rig, store the capture offset per model and OS with date, method, count and spread; expiry rules.
- `docs/calibration.md`.

Exit: repeat calibrations of one phone agree within their stated spread.

### M-P2: the app

- Camera + microphone capture, fiducial search, rolling-shutter row time, marker and chirp detection, pairing, grading with the analyzer's windows, statistics, local storage, comparison, export of the result JSON.
- UI of SPECIFICATION §12.1: offset with sign and sentence, quality, calibration state (uncalibrated in orange, no grade colour).
- Validation on real devices at a monitor fed by a generator with known set offsets (0, ±40 ms), against the live analyzer on the same feed.
- Store builds (TestFlight, internal Play track) — `TODO_` accounts.

Exit: calibrated phones agree with the live analyzer within their stated uncertainty on the validation set.

## Decisions

1. AV offset and latency are separate; latency only with a TAI/PTP reference (SPECIFICATION §3).
2. Offsets on index times, latencies on arrival times (§3.3).
3. Video events exactly on grain origins; no sub-frame video claims in 1.0 (§3.4).
4. Pairing by identity (Gray strip + four chirp shapes), ±2 periods (§4.3).
5. One dependency-free core for CLI, media function and phone (§1, §15).
6. One image, two catalog types, one role per process (§2.1, §17.2).
7. One measuring point per analyzer instance (§7.4).
8. Grading windows −5…+15 pass, −40…+60 warn, else fail; on the median of 10 events (§8.3).
9. No preview stream, no MediaMTX: the picture is in MXL, the result is numbers and a small flow.

## Deviations

1. **Text is Blend2D 0.21.2 with DejaVu Sans 2.37**, the same pin mxl-multiviewer uses. Blend2D and the asmjit in its release tarball are fetched at configure time and linked statically. The font file is `third_party/dejavu/DejaVuSans.ttf`, embedded by `cmake/EmbedBytes.cmake`. `draw_text` still writes white luma into the black boxes. Marker, strip, fiducials and QR are unchanged.
2. **QR decode is quirc (ISC), not zxing-cpp.** Encode is still Nayuki, error correction M. quirc reads the codes this generator writes. zxing can replace `decode_qr_text`.
3. **NMOS is an in-tree IS-04 v1.3 / IS-05 v1.1 node, not nmos-cpp.** It exposes the node, device, flows, senders and receivers, the test-signal tag, group hints, and `mxl_domain_id` / `mxl_flow_id` on `urn:x-nmos:transport:mxl`. It does not run the AMWA test suite and it never sends IS-05 to another device. nmos-cpp at `fe303849` remains the pin if a later build swaps the module.
4. **libmxl is not linked in the default binary.** The pin is recorded (`MXL_REF=218ddaa0a08c12ffe75fc475ae65aa3d9eef16d7`). Ubuntu 24.04's fmt/spdlog packages are older than the MXL v1.1 build, so this VM did not produce a live writer. File round trips and the control API are what this environment can prove.
5. **Interlaced file pictures are not a separate mux path yet.** The event plan for 1080i25 is tested (events on first fields, even period). The file generator's exercised rasters are progressive.

## What was measured

- Clean PCM p99 timing error ~0 ms; SNR 10 dB and G.711 detection 100 %. Threshold stays 0.5. Chirp shapes stay as specified. Audio uncertainty at correlation ≥ 0.95 is 0.10 ms so the band-pass row (p99 0.054 ms) is covered. Details: `docs/test-matrix.md`.
- MOV + ProRes + PCM: offsets 0, +35, −35 and a sweep, deviation within 1 ms, container checks pass (video 1/50, audio 1/48000). An ffmpeg `adelay` of 20 ms reads back as +20.0 ms.
- H.264 + AAC is not exact (about −19 ms on a 3 s 720p50 clip) and is only offered with `--allow-container-shift`.
- `serve`: `/livez` 200, `/readyz` 503 without flows, metrics families present at 0, widget CSP and no `X-Frame-Options`, unknown widget 404, bad parameter 400, invalid config 78, SIGTERM 143.

## Stages 2 and 3 (2026-10-24)

Implemented on top of the stage 1 branch. `mxl-avsync paths` lists what runs here and what still needs the host.

- Fiducial search (`locate_picture`, `read_located_marker`) and `analyze --locate`.
- File-mode H.264 at 1/4/10 Mbit/s and G.711 PCMU/PCMA at 8 kHz (`tests/integration/stage2.sh`).
- Cross-node merge view: CLI `merge` and `POST /api/v1/merge`, using arrival time when both sides are `tai-ptp`.
- Phone core: rolling-shutter row time, 90-day calibration, `PhoneSession`. Expo UI, display rules, and the LED/piezo firmware are in `mobile-app/`.

6. **Live SRT, the fabrics agent, and live SIP are not processes in this repo.** The file-mode distortion (H.264, G.711) and the merge view are implemented. The gateway, the agent, and a SIP function are host gaps.
7. **The phone UI is not executed on a device here.** Capture sources for iOS (host clock) and Android (`REALTIME` sensor timestamps) are in `mobile-app/modules/avsync/`. They are not compiled by CMake.

The on-screen sync scale is a moving square, not the configured offset. Each step is one frame. At 1080p50 and 2/s that is ±240 ms, one tick per frame, crossed once per event: red while early, white at 0, green while late. The analyzer still reads only the marker block and the Gray strip. Under the centre square, a PNG or GIF sits immediately left of the source name, with `AVSYNC_SUBTITLE` on the next line (`--subtitle`, `PATCH /api/v1/generator`). `web/icon.png` is the app icon on the test signal, the web GUI, and the phone app.

## Next step

On the Linux host: link libmxl, then the SRT gateway loop, a two-node fabrics merge, and a device build of the Expo app against the calibration rig. ST 2110 remains the stage 1 host gap.
