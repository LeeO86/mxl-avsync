# mxl-avsync — Implementation plan

This plan comes before any code. It is updated as the work goes on: every decision and every deviation from `SPECIFICATION.md` is recorded here, as in the sibling repos.

## Status (2026-10-24)

Core, file tools, the serve control plane, the live MXL reader and writer, the PTP status reader, QR decode with zxing-cpp 2.3.0, and the container image are implemented. `avsync-core` does not link libmxl. `mxlio/` is linked only into `mxl-avsync` when `find_package(mxl)` succeeds. `/readyz` is 200 once the video and audio flows are open on a tmpfs domain. This repo reads and writes MXL flows and files only. It does not implement ST 2110, SRT, SIP, fabrics or WebRTC. The ST 2110 hour run goes through `mxl-st2110-gateway` (generator → MXL → gateway → network → gateway → MXL → analyzer). SRT and SIP/G.711 live inputs arrive as MXL flows from the sibling gateways. File-mode H.264 and G.711 are test aids only. Stage 2 is "use the sibling gateways, test the matrix", with no transport code here. The phone app remains later and does not block live MXL. Decisions of §18 still hold, including the rewritten path decision. Deviations and limits are below.

## Pins

| Component | Pin |
| --- | --- |
| MXL | `dmf-mxl/mxl` `release/v1.1` at `218ddaa0a08c12ffe75fc475ae65aa3d9eef16d7`, `-DMXL_ENABLE_FABRICS_OFI=OFF`; one `MXL_REF` in `docker/Dockerfile` and `.github/workflows/ci.yaml` |
| nmos-cpp | `fe303849527394b03bdedc8f161f377fe458bb62` (the siblings' commit) |
| Blend2D, DejaVu Sans | the versions mxl-multiviewer uses |
| FFmpeg | Ubuntu 24.04 system libraries (file mode, transcode tests) |
| zxing-cpp | 2.3.0, readers only, fetched at configure time. Encode stays Nayuki |
| Nayuki QR, pocketfft, doctest | vendored |

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
- The robustness suite of SPECIFICATION §14.1 for audio (level, noise, AGC, band-pass, resampling, G.711 μ-law/A-law as a table-codec test aid, gaps, cross-confusion) with a results table: timing error p50/p99 and detection rate per condition.
- Decide: chirp shapes, default threshold, the uncertainty table. Record them here.

Exit: the audio detector meets ≤ 0.1 ms p99 timing error on the clean PCM path and ≥ 99 % detection at SNR 10 dB and through the G.711 test aid; or this plan records what it reaches and the spec is adjusted before M1.

### M1: test picture and the file tools (draft phases 3–4: the draft's MVP)

- Picture composition with Blend2D: static layer, timing marker, Gray strip, sweep bar, counters, QR (Nayuki), TEST SIGNAL, fiducials; v210 and 8-bit RGB for files.
- Marker detector on luma (thresholds from the reference blocks, Gray decode, parity, drops and duplicates, quality) and its synthetic-frame tests.
- QR decode with zxing-cpp.
- `mxl-avsync generate`: MOV + ProRes 422 HQ + PCM 24-bit by default, MP4/MKV/AAC/H.264 variants; manifest; the container check (SPECIFICATION §9.3).
- `mxl-avsync analyze`, `validate`, `merge`: decode with FFmpeg keeping PTS, pairing, grading, statistics, human and JSON reports, `--fail-on`.
- `tests/integration/file.sh`: offsets 0, +35, −35, a sweep, inserted delays, and the file-mode test aids of SPECIFICATION §13 (H.264 bitrates, scaling, frame-rate conversion, resampling, level, AGC, G.711). Those codecs are test aids, not live inputs. Results into `docs/test-matrix.md`.
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

### M5: hardening and release 1.0.0

- No ST 2110, SRT or SIP code. The hour run is not a task of this repo: it goes through `mxl-st2110-gateway` (generator → MXL → gateway → network → gateway → MXL → analyzer). When that lab is run, the numbers can be copied into `docs/test-matrix.md` with the gateway version. SRT and SIP/G.711 live inputs are MXL flows from the sibling gateways.
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
| 1.1 | Use the sibling gateways and test the matrix. No transport code in this repo. The ST 2110 hour run goes through `mxl-st2110-gateway`. SRT (`mxl-srt-gateway`) and SIP/G.711 arrive as MXL flows. Cross-node movement is `mxl-fabrics-agent`; this repo only merges analyzer results it already has. File-mode H.264 and G.711 stay test aids |
| 2.0 | the phone app (M-P0 to M-P2 below) |
| later | per-channel sequences (channel-swap test), lip-sync on programme material, IS-07 events |

### M-P0: phone platform spike (after 1.0; fiducial search is already in the tree)

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
2. **QR decode is zxing-cpp 2.3.0, the decoder named in the spec.** The earlier quirc deviation is closed. Encode is still Nayuki, error correction M. `avsync-core` does not link zxing. Writers are off, so the bundled zint encoder is not built.
3. **The NMOS node is nmos-cpp `fe303849` when `AVSYNC_WITH_NMOS` is on.** Ids stay `mxl-avsync/<seed>/...` from `util/uuid.cpp`. DNS-SD is off unless `NMOS_DNS_SD=true`. MXL connection resources are IS-05 v1.2, which is the version this pin implements for `urn:x-nmos:transport:mxl`. The process does not send IS-05 to another device. A build without nmos-cpp keeps the small in-tree node.
4. **libmxl is linked only into `mxl-avsync`, through `mxlio/`.** `avsync-core` stays free of it. The pin is `MXL_REF=218ddaa0a08c12ffe75fc475ae65aa3d9eef16d7`, built with vcpkg the way mxl-scopes builds it (`-DMXL_ENABLE_FABRICS_OFI=OFF`). A build without that package still runs the file tools and leaves `/readyz` at 503.
5. **Interlaced file pictures are not a separate mux path yet.** The event plan for 1080i25 is tested (events on first fields, even period). The file generator's exercised rasters are progressive.

## What was measured

- Clean PCM p99 timing error ~0 ms; SNR 10 dB and the G.711 table-codec test aid detection 100 %. Threshold stays 0.5. Chirp shapes stay as specified. Audio uncertainty at correlation ≥ 0.95 is 0.10 ms so the band-pass row (p99 0.054 ms) is covered. Details: `docs/test-matrix.md`.
- MOV + ProRes + PCM: offsets 0, +35, −35 and a sweep, deviation within 1 ms, container checks pass (video 1/50, audio 1/48000). An ffmpeg `adelay` of 20 ms reads back as +20.0 ms.
- H.264 + AAC is not exact (about −19 ms on a 3 s 720p50 clip) and is only offered with `--allow-container-shift`.
- `serve`: `/livez` 200, `/readyz` 503 without flows, metrics families present at 0, widget CSP and no `X-Frame-Options`, unknown widget 404, bad parameter 400, invalid config 78, SIGTERM 143.

## Stages 2 and 3 (2026-10-24)

Stage 2 is no longer a transport milestone. It is: use the sibling gateways and test the matrix. Nothing in this repo speaks ST 2110, SRT, SIP or fabrics. What is already in the tree:

- Fiducial search (`locate_picture`, `read_located_marker`) and `analyze --locate`.
- File-mode H.264 at 1/4/10 Mbit/s and G.711 PCMU/PCMA at 8 kHz (`tests/integration/stage2.sh`). These are test aids (FFmpeg), not live inputs.
- Merge of two analyzer results: CLI `merge` and `POST /api/v1/merge`, using arrival time when both sides are `tai-ptp`. The fabrics agent is not in this repo.
- Phone core: rolling-shutter row time, 90-day calibration, `PhoneSession`. Expo UI, display rules, and the LED/piezo firmware are in `mobile-app/`.

6. **No ST 2110, SRT, SIP or fabrics transport in this repo.** The hour run goes through `mxl-st2110-gateway`: generator → MXL → gateway → network → gateway → MXL → analyzer. SRT and live SIP/G.711 arrive as MXL flows from the sibling gateways. File-mode H.264 and G.711 are test aids only.
7. **The phone UI is not executed on a device here.** Capture sources for iOS (host clock) and Android (`REALTIME` sensor timestamps) are in `mobile-app/modules/avsync/`. They are not compiled by CMake.
8. **PTP state is the HTTP document at `AVSYNC_PTP_STATUS_URL`.** The sibling trees do not include a PTP reader, so this repo keeps the URL it already had. Only `http://` is fetched. The body is `{"sync_state":"locked"|"unlocked"|"unknown","accuracy_ns":"..."}`. Anything else, including a missing URL, leaves `sync_state` `unknown` and does not invent a latency.

The on-screen sync scale is a moving square, not the configured offset. Each step is one frame. At 1080p50 and 2/s that is ±240 ms, one tick per frame, crossed once per event: red while early, white at 0, green while late. The analyzer still reads only the marker block and the Gray strip. Under the centre square, a PNG or GIF sits immediately left of the source name, with `AVSYNC_SUBTITLE` on the next line (`--subtitle`, `PATCH /api/v1/generator`). `assets/app-icon.svg` is the app icon. The phone store and adaptive icons, the web favicon, touch icon, and header mark, and the test-signal mark are rasters of that file.

## Limits

- No transport code: not ST 2110, not SRT, not SIP, not fabrics, not WebRTC. Sibling repos move those flows onto MXL. This process only reads and writes MXL flows and files.
- The hour run is a lab through `mxl-st2110-gateway`, not a binary or a CI job here.
- File-mode H.264 and G.711 measure the detectors. They are not a substitute for a live gateway.
- Latency is filled only when `sync_state` is `locked` and the QR on the event grain names that event's `T_k`. An unknown or unlocked clock leaves `video_latency_ms`, `audio_latency_ms`, the index shifts and `av_offset_arrival_ms` absent. On this closed loop the generator writes audio `AVSYNC_AUDIO_LEAD_MS` ahead, so `audio_latency_ms` can be negative.
- `mxl-avsync paths` names `sibling-gateways` for ST 2110, SRT, SIP and fabrics. It does not list `srt-live`, `g711-sip`, `st2110-loop` or `fabrics-cross-node`.
- The phone UI has not been run on a device. Phone work does not block live MXL.

## Live MXL (2026-10-24)

Command, from a build configured with `-DCMAKE_PREFIX_PATH="/opt/mxl;/tmp/mxl/build/vcpkg_installed/x64-linux"`:

```
LD_LIBRARY_PATH=/opt/mxl/lib:/tmp/mxl/build/vcpkg_installed/x64-linux/lib \
  BIN=/tmp/avsync-live/mxl-avsync SAMPLE=/tmp/avsync-live/avsync-sample-flow \
  bash tests/integration/live.sh
```

Output:

```
offset 35: 3585748694 35.000244140625 warn
offset 0: 3585748696 0.000244140625 pass
offset -35 after reopen: 3585748698 -35.00048828125 warn
interlaced field grain
{"grain_rows":540,"height":1080,"interlaced":true,"media_ready":true,"width":1920}
index=89643717482 bytes=2764800 grain_rows=540 width=1920 height=1080 interlaced=1 valid=1
live.sh ok
```

The three offsets are the newest paired event after each generator start (1080p50, 2/s) on `/dev/shm`. The analyzer's expected offset was 0, so 35 ms and −35 ms grade warn and 0 ms grades pass. The interlaced grain is one field: 540 rows, 2 764 800 bytes, `interlaced_tff`. No latency is claimed.

## Result picture and JPEG previews (2026-10-24)

The analyzer writes a second flow, `uuid_v5("mxl-avsync/<seed>/flow/result/<format>")`, default 960×540 at 25/s. The picture is redrawn at 5 Hz. JPEG snapshots of the generated flow and the incoming flow are encoded on a worker thread, about once a second. `GET /api/v1/preview/input.jpg` and `GET /api/v1/preview/generated.jpg` return `image/jpeg`, or 404 `{"error":"no frame yet"}`. The web UI reloads both URLs once a second. There is no preview stream.

Same command as the closed loop above. The run that included the new checks printed:

```
offset 35: 3585749982 35.0 warn
jpeg 24287 bytes http://127.0.0.1:18480/api/v1/preview/generated.jpg
jpeg 24115 bytes http://127.0.0.1:18482/api/v1/preview/input.jpg
jpeg 5537 bytes http://127.0.0.1:18482/api/v1/preview/generated.jpg
index=44821874778 bytes=1382400 grain_rows=540 width=960 height=540 interlaced=0 valid=1
offset 0: 3585749984 0.0 pass
offset -35 after reopen: 3585749986 -35.0 warn
live.sh ok
```

The generator's input preview was HTTP 404 `no frame yet`. The analyzer result flow was 960×540, one progressive grain, 1 382 400 bytes. The analyzer's generated JPEG is that result tile; its input JPEG is the test signal it is reading.

## NMOS node (2026-10-24)

No registry and no mDNS. DNS-SD left at its default, off.

```
LD_LIBRARY_PATH=/opt/mxl/lib:/tmp/mxl/build/vcpkg_installed/x64-linux/lib \
  BIN=/tmp/avsync-nmos/mxl-avsync bash tests/integration/nmos.sh
```

```
node id 436cc13d-3c14-51cf-8bbb-6864a81d4368
sender flow 6f6cc5dc-a56c-52e0-89aa-db6d881bb860 domain d69b8f98-1ebb-58ae-9d82-2af8234d9f3e
analyzer ready after IS-05
taken port exits 75
nmos.sh ok
```

The node id is UUIDv5 of `mxl-avsync/nmos-gen/node`. Senders use `urn:x-nmos:transport:mxl` with empty `interface_bindings`. Activating the analyzer's video and audio receivers with the generator's domain and flow ids made `/readyz` 200 and `input_state` running. A second process on the same NMOS port exited 75. The AMWA nmos-testing suite was not run: this VM has no Docker, and a registry is not started with mDNS on the host network.

## PTP state (2026-10-24)

`serve` polls `AVSYNC_PTP_STATUS_URL` about once a second. `GET /api/v1/status` returns `ptp.sync_state` and `ptp.accuracy_ns`. Analyzer events copy that pair into `reference`. `mxl_avsync_reference_sync{type="tai-ptp",state="locked|unlocked|unknown"}` is 1 for the current state. `video_latency_ms` and `audio_latency_ms` stay absent: this build does not record arrival time, and index time is not used as a latency.

Command:

```
BIN=/tmp/avsync-live/mxl-avsync \
  VCPKG_LIB_DIR=/tmp/mxl/build/vcpkg_installed/x64-linux/lib \
  bash tests/integration/ptp.sh
```

Output:

```
ptp.sh work /dev/shm/avs-ptp-kMAjYi
locked:
{"audio_enable":true,"audio_flow":"0f194ea0-4d93-5fd2-94ab-29a1423af8ae","detail":"","grain_rows":1080,"height":1080,"input_state":"running","interlaced":false,"media_ready":true,"nmos_registered":false,"ptp":{"accuracy_ns":"80","sync_state":"locked"},"result_domain":"/dev/shm/avs-ptp-kMAjYi/ana","result_flow":"9f8fdeea-a396-5da3-b9ba-cbba87431431","role":"analyzer","stopped":false,"video_flow":"f92c7353-5c57-5238-851e-5ed47a25f0fd","width":1920}
event:
3585761511 0.0 pass locked 80
empty url:
{"audio_enable":true,"audio_flow":"","detail":"no flow","grain_rows":0,"height":0,"input_state":"waiting","interlaced":false,"media_ready":false,"nmos_registered":false,"ptp":{"accuracy_ns":"unknown","sync_state":"unknown"},"result_domain":"/dev/shm/avs-ptp-kMAjYi/empty","result_flow":"9f8fdeea-a396-5da3-b9ba-cbba87431431","role":"analyzer","stopped":false,"video_flow":"","width":0}
dead url:
{"audio_enable":true,"audio_flow":"","detail":"no flow","grain_rows":0,"height":0,"input_state":"waiting","interlaced":false,"media_ready":false,"nmos_registered":false,"ptp":{"accuracy_ns":"unknown","sync_state":"unknown"},"result_domain":"/dev/shm/avs-ptp-kMAjYi/dead","result_flow":"9f8fdeea-a396-5da3-b9ba-cbba87431431","role":"analyzer","stopped":false,"video_flow":"","width":0}
https refused:
{"audio_enable":true,"audio_flow":"","detail":"no flow","grain_rows":0,"height":0,"input_state":"waiting","interlaced":false,"media_ready":false,"nmos_registered":false,"ptp":{"accuracy_ns":"unknown","sync_state":"unknown"},"result_domain":"/dev/shm/avs-ptp-kMAjYi/https","result_flow":"9f8fdeea-a396-5da3-b9ba-cbba87431431","role":"analyzer","stopped":false,"video_flow":"","width":0}
unlocked:
{"audio_enable":true,"audio_flow":"","detail":"no flow","grain_rows":0,"height":0,"input_state":"waiting","interlaced":false,"media_ready":false,"nmos_registered":false,"ptp":{"accuracy_ns":"500","sync_state":"unlocked"},"result_domain":"/dev/shm/avs-ptp-kMAjYi/unlocked","result_flow":"9f8fdeea-a396-5da3-b9ba-cbba87431431","role":"analyzer","stopped":false,"video_flow":"","width":0}
ptp.sh ok
```

The event line is seq, offset, grade, `sync_state`, `accuracy_ns`. That event had no `video_latency_ms` and no `audio_latency_ms`.

## QR decode (2026-10-24)

`decode_qr_text` uses zxing-cpp 2.3.0 (readers only). Encode is still Nayuki. The picture test draws a 1080p frame and reads the QR payload back.

Command:

```
cmake -S . -B /tmp/avsync-qr -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/avsync-qr --target avsync-picture-tests -j
/tmp/avsync-qr/avsync-picture-tests
```

Output:

```
[doctest] doctest version is "2.4.11"
[doctest] run with "--help" for options
===============================================================================
[doctest] test cases:  2 |  2 passed | 0 failed | 0 skipped
[doctest] assertions: 31 | 31 passed | 0 failed |
[doctest] Status: SUCCESS!
```

The case "large qr stays decodable and the sync square moves" compares the decoded text with the Nayuki payload for test id `TEST-IDENT`, event 4, `tk` 1000, format `1080p50`.

## Packaging and ops (2026-10-24)

Every exported metric family is present from process start, counters at 0. `video_latency_ms` and `audio_latency_ms` are not exported: a published 0 would be a false latency. The names are in the changelog. Samples of each output-frame family are contiguous.

The image is multi-stage. The build stage runs `avsync-core-tests` and `avsync-picture-tests`. The runtime stage is `USER 1000:1000`, labelled `io.dmf.mxl.revision` from the `MXL_REF` build arg. There is no `cap_sys_nice`: the writers sleep on TAI and do not use real-time scheduling. The release workflow pushes `ghcr.io/leeo86/mxl-avsync`. CI jobs are `dockerfile`, `image`, `test`, and `build-and-test`. `MXL_REF` is the workflow env and the Dockerfile `ARG`.

Command:

```
LD_LIBRARY_PATH=/opt/mxl/lib:/tmp/mxl/build/vcpkg_installed/x64-linux/lib \
  BIN=/tmp/avsync-qr/mxl-avsync bash tests/integration/metrics.sh
```

Output:

```
# HELP mxl_avsync_info Build information.
# TYPE mxl_avsync_info gauge
mxl_avsync_info{version="1.0.0",core_version="1.0.0",mxl_revision="218ddaa0a08c12ffe75fc475ae65aa3d9eef16d7",role="generator"} 1
# TYPE mxl_avsync_generator_active gauge
mxl_avsync_generator_active 0
# TYPE mxl_avsync_generator_audio_muted gauge
mxl_avsync_generator_audio_muted 0
# TYPE mxl_avsync_generator_events_total counter
mxl_avsync_generator_events_total 0
# TYPE mxl_avsync_output_frames_total counter
mxl_avsync_output_frames_total{essence="video"} 0
mxl_avsync_output_frames_total{essence="audio"} 0
# TYPE mxl_avsync_output_frames_late_total counter
mxl_avsync_output_frames_late_total{essence="video"} 0
mxl_avsync_output_frames_late_total{essence="audio"} 0
# TYPE mxl_avsync_output_frames_missed_total counter
mxl_avsync_output_frames_missed_total{essence="video"} 0
mxl_avsync_output_frames_missed_total{essence="audio"} 0
# TYPE mxl_avsync_input_state gauge
mxl_avsync_input_state{essence="video",state="waiting"} 0
mxl_avsync_input_state{essence="audio",state="waiting"} 0
mxl_avsync_input_state{essence="video",state="no_signal"} 0
mxl_avsync_input_state{essence="audio",state="no_signal"} 0
mxl_avsync_input_state{essence="video",state="not_routed"} 0
mxl_avsync_input_state{essence="audio",state="not_routed"} 0
mxl_avsync_input_state{essence="video",state="running"} 0
mxl_avsync_input_state{essence="audio",state="running"} 0
mxl_avsync_input_state{essence="video",state="format_mismatch"} 0
mxl_avsync_input_state{essence="audio",state="format_mismatch"} 0
# TYPE mxl_avsync_input_resyncs_total counter
mxl_avsync_input_resyncs_total{essence="video"} 0
mxl_avsync_input_resyncs_total{essence="audio"} 0
# TYPE mxl_avsync_events_total counter
mxl_avsync_events_total{result="paired"} 0
mxl_avsync_events_total{result="missing_audio"} 0
mxl_avsync_events_total{result="missing_video"} 0
mxl_avsync_events_total{result="duplicate"} 0
mxl_avsync_events_total{result="ambiguous"} 0
mxl_avsync_events_total{result="out_of_range"} 0
# TYPE mxl_avsync_grade gauge
mxl_avsync_grade{grade="pass"} 0
mxl_avsync_grade{grade="warn"} 0
mxl_avsync_grade{grade="fail"} 0
mxl_avsync_grade{grade="invalid"} 0
# TYPE mxl_avsync_av_offset_ms gauge
# TYPE mxl_avsync_av_offset_deviation_ms gauge
mxl_avsync_av_offset_ms 0
mxl_avsync_av_offset_deviation_ms 0
# TYPE mxl_avsync_av_offset_seconds histogram
mxl_avsync_av_offset_seconds_bucket{le="-0.5"} 0
mxl_avsync_av_offset_seconds_bucket{le="-0.2"} 0
mxl_avsync_av_offset_seconds_bucket{le="-0.1"} 0
mxl_avsync_av_offset_seconds_bucket{le="-0.06"} 0
mxl_avsync_av_offset_seconds_bucket{le="-0.04"} 0
mxl_avsync_av_offset_seconds_bucket{le="-0.015"} 0
mxl_avsync_av_offset_seconds_bucket{le="-0.005"} 0
mxl_avsync_av_offset_seconds_bucket{le="0.005"} 0
mxl_avsync_av_offset_seconds_bucket{le="0.015"} 0
mxl_avsync_av_offset_seconds_bucket{le="0.04"} 0
mxl_avsync_av_offset_seconds_bucket{le="0.06"} 0
mxl_avsync_av_offset_seconds_bucket{le="0.1"} 0
mxl_avsync_av_offset_seconds_bucket{le="0.2"} 0
mxl_avsync_av_offset_seconds_bucket{le="0.5"} 0
mxl_avsync_av_offset_seconds_bucket{le="+Inf"} 0
mxl_avsync_av_offset_seconds_sum 0
mxl_avsync_av_offset_seconds_count 0
# TYPE mxl_avsync_video_drops_total counter
# TYPE mxl_avsync_video_duplicates_total counter
mxl_avsync_video_drops_total 0
mxl_avsync_video_duplicates_total 0
# TYPE mxl_avsync_audio_correlation gauge
# TYPE mxl_avsync_audio_level_dbfs gauge
mxl_avsync_audio_correlation 0
mxl_avsync_audio_level_dbfs 0
# TYPE mxl_avsync_reference_sync gauge
mxl_avsync_reference_sync{type="tai-ptp",state="locked"} 0
mxl_avsync_reference_sync{type="tai-ptp",state="unlocked"} 0
mxl_avsync_reference_sync{type="tai-ptp",state="unknown"} 0
# TYPE mxl_avsync_nmos_registry_up gauge
mxl_avsync_nmos_registry_up 0
# TYPE mxl_avsync_nmos_activations_total counter
mxl_avsync_nmos_activations_total 0
metrics.sh ok
```

This VM has no Docker, so the image was not built here. `docker buildx build --check` is the `dockerfile` CI job.

## Arrival time when PTP is locked (2026-10-25)

The live QR is refreshed on each event and carries that event's `k` and `tk`. The analyzer reads it on the event grain. With `sync_state` `locked`, the event gains `video_latency_ms` (`t_arr − T_k`), `audio_latency_ms` (`t_arr − (T_k + offset)`), `video_index_shift_ms`, `audio_index_shift_ms` (both against the set time, 0 when the index time matches), and `av_offset_arrival_ms`. An unknown clock does not get those fields. A unit case checks the gate.

Command:

```
/tmp/avsync-qr/avsync-core-tests -tc='*drawing cost*'
/tmp/avsync-qr/avsync-core-tests -tc='*locked ptp*'
LD_LIBRARY_PATH=/opt/mxl/lib:/tmp/mxl/build/vcpkg_installed/x64-linux/lib \
  BIN=/tmp/avsync-qr/mxl-avsync VCPKG_LIB_DIR=/tmp/mxl/build/vcpkg_installed/x64-linux/lib \
  bash tests/integration/ptp.sh
/tmp/avsync-qr/mxl-avsync paths
```

The drawing case was run on its own and printed `draw_1080p_ms=0.206943`. The locked-ptp case passed 17 assertions. The live event line was:

```
3585774266 0.0 pass locked 80 7.03955078125 -12.231689453125 0.0 0.0 -19.271240234375
```

That is seq, offset, grade, sync, accuracy, `video_latency_ms`, `audio_latency_ms`, `video_index_shift_ms`, `audio_index_shift_ms`, `av_offset_arrival_ms`. Index shift is 0. Video arrival is 7.0 ms after `T_k`. Audio arrival is 12.2 ms before its set time, which matches the generator writing audio ahead. `paths` prints `sibling-gateways` and does not print the four old names.

Host for that run: 4 CPUs, `Intel(R) Xeon(R) Processor`, analyzer user time 0.110 s for the short closed loop. Catalog CPU requests stay the spec placeholders. This is not a Skylake-SP or 8-core desktop figure.

`docker/docker-compose.demo.yaml` and `deploy/k8s/avsync.yaml` start both roles on a shared memory volume with NMOS and DNS-SD off. They were not started: this machine has no Docker and no cluster.

## Content time against the grain timestamp (2026-10-25)

`mxlGrainInfo` at this pin carries the grain time as `index`. `mxlIndexToTimestamp` turns that field into the grain's TAI timestamp. The audio value is the same call on the chirp's sample index. No other clock is used.

Live results name:

- `video_content_vs_grain_ms` — QR `T_k` minus the video grain timestamp
- `audio_content_vs_grain_ms` — chirp time minus that sample's MXL timestamp
- `content_vs_grain_av_ms` — the video difference minus the audio difference

These three are reported when `sync_state` is unknown. Arrival latency stays absent then. When the clock is locked, the result also names `video_content_vs_grain_vs_latency_ms`, `audio_content_vs_grain_vs_latency_ms`, `video_content_vs_grain_vs_index_shift_ms`, `audio_content_vs_grain_vs_index_shift_ms`, and `content_vs_grain_av_vs_arrival_ms`.

Command:

```
/tmp/avsync-qr/avsync-core-tests -tc='*content versus grain*'
LD_LIBRARY_PATH=/opt/mxl/lib:/tmp/mxl/build/vcpkg_installed/x64-linux/lib \
  BIN=/tmp/avsync-qr/mxl-avsync SAMPLE=/tmp/avsync-qr/avsync-sample-flow \
  VCPKG_LIB_DIR=/tmp/mxl/build/vcpkg_installed/x64-linux/lib \
  bash tests/integration/live.sh
```

The unit case passed 20 assertions. With no PTP URL the closed loop printed:

```
content vs grain 3585808608 0.0 0.0 0.0
```

That is seq, `video_content_vs_grain_ms`, `audio_content_vs_grain_ms`, `content_vs_grain_av_ms`. The same run's offset line was `3585808608 35.0 warn`, and `live.sh` ended `live.sh ok`. Arrival fields were absent.

## Next step

Left on purpose: the AMWA suite (no Docker, and no mDNS registry on this host), a separate interlaced file mux (the plan does not ask for one), stage 4 (per-channel sequences, programme lip-sync, IS-07), the phone on a device, the LED/piezo rig, and the ST 2110 hour run.
