# mxl-avsync — Specification

Status: Draft v0.2 (design only; no code exists yet)
Repository: `LeeO86/mxl-avsync`
Origin: the German product draft "MXL/DMF AV-Sync- und End-to-End-Latenz-Messsystem" v1.0 (2026-10-23), kept unchanged in [`docs/draft-2026-10-23-de.md`](docs/draft-2026-10-23-de.md). This document reworks it into the form of the sibling specifications and takes the maintainer's decisions of 2026-10-23 (§18).
Aligns with: the media function guideline G1–G14 and the later platform conventions (preview contract, widgets, release notes) as written out in `LeeO86/mxl-audio-mixer` SPECIFICATION Appendices A and B, and the sibling repos `LeeO86/mxl-scopes` (closest sibling: one input per instance, result picture as an MXL flow), `mxl-test-player` (pattern writer, file playback), `mxl-multiviewer` (interlaced convention, Blend2D text), `mxl-st2110-gateway` (the ST 2110 hour run of §13 goes through it; this repo does not speak ST 2110), `mxl-srt-gateway` (SRT arrives here as an MXL flow), `mxl-audio-mixer` (spec structure, real-time audio lessons).

The key words MUST, MUST NOT, SHOULD, SHOULD NOT and MAY are used as in RFC 2119.

Names that are guesses and need a check before implementation are marked `TODO_`.

---

## 1. Purpose and scope

`mxl-avsync` measures audio/video synchronisation (AV offset) and, where a common time reference exists, audio, video and end-to-end latency through a media chain. It consists of:

1. a **generator** that writes a visible and audible test signal: live into MXL flows, or as deterministic test files with a manifest;
2. an **analyzer** that finds the video markers and audio chirps in MXL flows or in files, pairs them, and reports the offset, the latencies, their quality and statistics;
3. both as one **media function** image on the platform, with two catalog types (`avsync-generator`, `avsync-analyzer`);
4. a **phone app** (Expo, iOS and Android, the last milestone) that measures the AV offset at a real monitor and loudspeaker with the phone's camera and microphone.

Design principles:

1. **AV offset and latency are different quantities.** AV offset needs no common clock. Verified absolute latency needs one (TAI from PTP on the platform); phone-clock estimates are separately marked, with unknown accuracy (§3).
2. **Measure, don't guess.** Every result carries a quality and an uncertainty, or says `unknown`. A missing or ambiguous marker gives an invalid measurement, never an invented number. No accuracy is claimed that a test has not shown.
3. **One portable core.** Event model, chirp synthesis and detection, marker drawing and detection, pairing and statistics live in `avsync-core`: C++20 with no dependencies on MXL, NMOS, FFmpeg or a GUI. The CLI, the media function and later the phone app (§12) use the same core.
4. **Test signals are obvious.** The picture says TEST SIGNAL, the NMOS labels and tags say so, the function never routes anything by itself (§11).
5. **One measuring point per analyzer instance.** One video and one audio input per instance, like mxl-scopes (§7.4). More points mean more instances.
6. **Platform conventions.** NMOS IS-04/IS-05 with BCP-007-03, env > file > defaults, web UI and REST on `WEB_PORT`, `/livez`, `/readyz`, `/metrics`, widgets, exit codes 0/75/78/143, GHCR image with the MXL revision label (Appendix A and B of the convention).

### 1.1 Stages

| Stage | Release | Content |
| --- | --- | --- |
| 1 | 1.0 (this spec, normative) | Event model and schemas. Test pattern and chirps. File generator and file analyzer (CLI). Live generator and live analyzer on MXL with TAI latency. Grading (pass/warn/fail), statistics, metrics, REST, web UI, the `avsync` widget, the result video flow, Grafana dashboard and alert rules. Paths in this repo: the MXL reference path and file round trips. File-mode H.264 and G.711 are test aids only (§13) |
| 2 | 1.1 (informative) | Use the sibling gateways and test the matrix. No transport code in this repo. The ST 2110 hour run goes through `mxl-st2110-gateway`. SRT and SIP/G.711 live inputs arrive as MXL flows from the sibling gateways |
| 3 | 2.0 (informative) | The phone app (Expo, iOS and Android) with capture calibration (§12) |
| 4 | later (informative) | Several chirp sequences per channel (channel-swap test). Lip-sync on real programme (no test signal). IS-07 events |

Out of scope for every stage: changing production routes, compressed MXL outputs, GStreamer in the media path, authentication, loudness measurement, and any transport. This repo reads and writes MXL flows and files. ST 2110, SRT, SIP, fabrics and WebRTC belong to the sibling gateways.

---

## 2. Architecture

```
              NMOS controller (production up, designer, Sofie)
                 │ IS-05 (mxl_domain_id, mxl_flow_id)
 ┌───────────────▼──────────────┐            ┌──────────────────────────────────┐
 │ mxl-avsync  AVSYNC_ROLE=generator          │ mxl-avsync  AVSYNC_ROLE=analyzer  │
 │                               │            │                                  │
 │ event clock (TAI)             │  MXL       │ video reader ─┐                  │
 │  ├ video writer: pattern ─────┼──flows ──► │ audio reader ─┤ (MXL flows;      │
 │  │  (Blend2D, QR, markers)    │            │               │  via gateways)   │
 │  └ audio writer: chirps ──────┼──────────► │ detectors (avsync-core)           │
 │ nmos-cpp · REST · /metrics    │            │ pairing → grading → statistics    │
 └───────────────────────────────┘            │ result writer ─► "Result Video"   │
                                              │ nmos-cpp · REST · WS · widget ·   │
                                              │ /metrics → Prometheus → alerts    │
                                              └──────────────────────────────────┘

 CLI  mxl-avsync generate  →  test.mov + manifest.json  →  (DAW, NLE, player, test-player)
 CLI  mxl-avsync analyze   ←  recording or transcode     →  result.json + text report

 Phone app (stage 3): camera + microphone → avsync-core (native module) → offset on screen
```

### 2.1 Roles

One image, one binary, three ways to run it:

| Way | Started as | Use |
| --- | --- | --- |
| CLI | `mxl-avsync generate …`, `mxl-avsync analyze …`, `mxl-avsync validate …` | test files, offline analysis, CI |
| Live generator | `mxl-avsync serve` with `AVSYNC_ROLE=generator` | the `avsync-generator` catalog type |
| Live analyzer | `mxl-avsync serve` with `AVSYNC_ROLE=analyzer` | the `avsync-analyzer` catalog type |

A `serve` process has exactly one role. One generator can feed any number of analyzers at different points of a chain.

### 2.2 Generator threads

| Thread | Job | Never waits for |
| --- | --- | --- |
| Video writer | One grain per output index from TAI. Copies the static layer and draws the changing parts (§5.4). Commits with `validSlices = totalSlices` | HTTP, NMOS |
| Audio writer | Writes the chirp signal in blocks (default 10 ms) ahead of TAI by `AVSYNC_AUDIO_LEAD_MS`; every sample is a pure function of its index (§6.1) | video writer |
| QR renderer | Draws the next QR code ahead of time (once per QR period) | writers |
| HTTP, WebSocket, NMOS | nmos-cpp and the web server threads | media threads |

### 2.3 Analyzer threads

| Thread | Job | Never waits for |
| --- | --- | --- |
| Video reader | Follows the routed video flow, waits for each grain, stamps its arrival time (§3.3), runs the marker detector on the marker regions only (§5.5), hands detections to the pairer. Handles `waiting`, `no_signal`, resync, flow re-creation, format change (as mxl-scopes §2) | pairer, result writer |
| Audio reader | Follows the routed audio flow in windows of `AVSYNC_AUDIO_WINDOW_MS`, stamps arrival, runs the chirp detector on one channel (default: the first) | video reader |
| QR worker | Decodes the QR region of a grain at most `AVSYNC_QR_RATE` times per second, off the media threads | readers |
| Pairer | Pairs video and audio events by event id (§8), grades, keeps the statistics | readers |
| Result writer | Draws the result picture at its own update rate (default 5 Hz) and writes it at the output rate (TAI paced, as the mxl-scopes writer) | pairer |
| HTTP, WebSocket, NMOS | as the generator | media threads |

The readers MUST NOT copy whole grains. The marker detector reads only its regions in place in the MXL ring and checks afterwards that the grain was not overwritten (as mxl-scopes §2.2).

---

## 3. Quantities and time model

### 3.1 Quantities

| Quantity | Definition | Needs |
| --- | --- | --- |
| **AV offset** | `av_offset_ms = audio_event_time_ms − video_event_time_ms`, both at the measuring point | nothing beyond the signal |
| **Deviation** | `deviation_ms = av_offset_ms − expected_av_offset_ms` (the generator's set offset) | the event's expected offset (QR or manifest) |
| **Video latency** | time the video event is seen at the measuring point − time it was emitted | a common clock |
| **Audio latency** | same for audio | a common clock |
| **End-to-end latency** | latency between two measuring points (source and destination analyzers, or generator and one analyzer) | a common clock |

### 3.2 Sign convention (binding for APIs, logs, UI, metrics and tests)

`av_offset_ms = audio_event_time_ms − video_event_time_ms`

- **Positive:** audio is late (behind the picture).
- **Negative:** audio is early (ahead of the picture).
- **Zero:** in sync within the measurement's resolution.

The UI always shows the sign and a sentence: `+35 ms — audio 35 ms after video`.

### 3.3 Modes and time domains

**FILE.** The timeline starts at `t = 0` at the start of the file. The manifest (§9.2) carries event ids, set times and the set offset. The real timestamps in the written container are checked and recorded (§9.3). File times never prove an absolute playback latency. Unix epoch MAY be used to label a file timeline; it is then marked `timeline: "file"` and never confused with a capture or TAI time.

**LIVE (MXL).** Every MXL grain and sample index maps to a TAI time (`indexToTimestamp`). The platform's nodes discipline that clock from PTP (domain 0 in the lab). The generator places event `k` at TAI time `T_k` (§4.2) and writes `T_k` into the QR payload, so an analyzer needs no manifest. At the analyzer two times exist for each detected event:

| Time | How | Meaning |
| --- | --- | --- |
| **Index time** `t_idx` | TAI time of the grain index (video) or of the sample index plus the sub-sample peak position (audio) | the media time the chain stamped on the content |
| **Arrival time** `t_arr` | TAI clock (`mxlGetTime`) when the reader saw the grain or sample window become available | when the content was there to be read |

From these:

- `av_offset_ms` uses **index times**. MXL readers (multiviewer, monitor, encoders) align audio and video by index, so the index offset is what a downstream viewer sees. This value is graded (§8.3).
- `video_index_shift_ms = t_idx(video) − T_k`, `audio_index_shift_ms` alike: how much the chain re-stamped the content (0 when every stage keeps the origin time). `mxl-st2110-gateway` keeps that origin with RTP timestamps; checking a gateway version is that repo's job, not a milestone here.
- `video_latency_ms = t_arr(video) − T_k`, `audio_latency_ms` alike: the real latency through the chain up to this point.
- `av_offset_arrival_ms = t_arr(audio) − t_arr(video)`: informative, for functions that read "the newest available" rather than by index.

Arrival time has the resolution of the reader's wake-up (target ≤ 1 ms, measured in M3) and is coarsened by writers that commit in batches (for example 960 samples per video frame, or packet bursts in a gateway). The reader MUST NOT count ranges that are not yet committed as late, and the uncertainty of every latency includes the batch size seen on that flow (§8.4).

**LIVE without a verified source reference.** AV offset remains a measurement. By explicit maintainer decision (2026-10-27), the iPhone source/capture workflows additionally expose **estimated latency**, with unknown clock/output/capture uncertainty. A phone's UTC is not a verified PTP reference; a locked server clock does not validate it. Phone-source packets and QR mark this distinction, and results retain `latency_estimated: true`. See [phone generator](docs/phone-generator.md). FILE playback never proves absolute latency.

**Every result names its reference:** `reference: {type: "tai-ptp" | "file" | "none", domain, sync_state, accuracy_ns | "unknown"}`. A configured `AVSYNC_PTP_STATUS_URL` explicitly wins (`sync_source=url`). Otherwise, a reachable, valid `AVSYNC_PTP_SOCKET` supplies ptp4l management status (`sync_source=ptp4l`); absent or failed socket access falls back to read-only kernel state (`sync_source=kernel`). A valid unlocked PTP response is retained. Kernel lock requires STA_UNSYNC clear and a positive TAI offset. Real `esterror` must be within `AVSYNC_CLOCK_MAX_ERROR_US`; when both `esterror` and `maxerror` are at least 16000000 µs they are unreported, accuracy is unknown and the reason is "error not reported (PTP discipline)". Kernel state cannot identify PTP versus NTP. See [Clock reference](docs/clock-reference.md) for PTP mapping and runtime requirements. The URL override is an `http://` URL. That URL must return JSON `{"sync_state":"locked"|"unlocked"|"unknown","accuracy_ns":"<decimal ns or unknown>"}`. With a URL set, any other scheme or a failed read leaves `sync_state` `unknown`. Latency numbers are reported only from a measured arrival time while `sync_state` is `locked`.

### 3.4 Resolution

- **Audio:** chirp peak by matched filter with sub-sample interpolation; target ≤ 0.1 ms on the PCM path, measured in M0 per condition (§14.1).
- **Video:** one grain. The generator puts every video event exactly at a grain origin, so on a path without frame-rate conversion the video index time is exact. With a frame-rate conversion or a frame drop near an event, the uncertainty is one input frame, and the result says so. Sub-frame video timing is not claimed in 1.0. (The sweep bar of §5.3 MAY give sub-frame timing to the phone app later; only after a test shows it.)
- **Interlaced:** one MXL grain is one field (height / 2 lines, indices at field rate, grain 2k = first field), following the shared MXL interlaced convention (https://github.com/LeeO86/mxl-multiviewer/blob/main/docs/mxl-interlaced.md). Video events fall on a first field; resolution is one field. Bottom-field-first input is refused (`format_mismatch`), as in the siblings.

---

## 4. Event model

### 4.1 Event

An event is one video marker change and one chirp that belong together.

| Field | Meaning |
| --- | --- |
| `event_id` | `EVT-` + zero-padded sequence number (`EVT-000024`). In LIVE mode the sequence number is `k` of §4.2 (it fits in 64 bits for any TAI time) |
| `seq` | the number `k` |
| `video_time` | set time of the video marker change (ms in FILE, TAI ns in LIVE) |
| `audio_time` | set time of the chirp's reference point (onset for audio timecode v3; centre in legacy mode) |
| `expected_av_offset_ms` | `audio_time − video_time` as planned |
| `chirp_id` | which of the four chirps (§6.1): `seq mod 4` |
| `marker_phase` | the marker state after the change (`seq mod 2`) |

### 4.2 Event plan

- Events come at `AVSYNC_EVENT_RATE` per second (1, 2 or 4; default 2). The period MUST be an integer number of video frames: 2/s at 50p = every 25 frames; at 59.94p 2/s is not integer, so the plan uses 30 frames (≈ 2.0 /s) and records the true period.
- LIVE: event `k` is at TAI `T_k = k × period` counted from the TAI epoch, so every generator with the same settings places its events at the same times, and an analyzer can predict the next event. `T_k` is aligned to the grain grid of the video flow.
- FILE: the first event is at `AVSYNC_FIRST_EVENT_MS` (default 1000), then every period, up to the file's end minus one period.
- Expected offset: `AVSYNC_AV_OFFSET_MS` (default 0, range −500…+500). The chirp is placed at `video_time + offset`, rounded to the nearest audio sample. The rounding residual is recorded in the manifest (FILE) and is 0 in LIVE (the chirp is synthesised at sub-sample precision, §6.1).
- An **offset sweep** for file tests MAY step the offset per event through a list (`AVSYNC_AV_OFFSET_SWEEP=-40,-20,0,20,40`); each event carries its own expected offset.
- The plan is deterministic: the same configuration gives the same plan, byte for byte (a unit test).

### 4.3 Pairing range

Events are paired by identity, not by nearest neighbour:

- The video event id comes from the frame counter strip (§5.2) of the grain where the marker changed (and from the QR code when it was read).
- The audio event id comes from the chirp id (`seq mod 4`) plus the predicted sequence.
- This pairs offsets without ambiguity up to ±2 periods (±1 s at 2/s). An offset beyond that is reported as `out_of_range`, not as a wrong small number.

---

## 5. Test video

### 5.1 Layout

1920×1080 reference layout; other rasters scale it in proportion (all positions in units of the raster height). Every element is in limited-range BT.709 Y′CbCr, v210.

```
┌──────────────────────────────────────────────────────────────────────────┐
│ ◣ fiducial                       TEST SIGNAL — mxl-avsync          fid ◢ │
│ ┌────────┐  test name, source, path, format, rate, audio, set offset,    │
│ │  QR    │  notes (free text)                                            │
│ └────────┘                                                               │
│                 ┌──────────────────────────────┐                         │
│   sweep bar ▶   │                              │   frame no. 0001234     │
│ ────────●──|──  │      TIMING MARKER           │   TAI 2026-10-23 …      │
│                 │  (black / white per event)   │                         │
│                 └──────────────────────────────┘                         │
│ ▮▯▮▮▯▯▮▯▮▯▮▮▯▮▯▯▮▯▮▯▮▮▯▮  frame counter strip (Gray code)                │
│ ◤ fid                           TEST SIGNAL                         fid ◥│
└──────────────────────────────────────────────────────────────────────────┘
```

1. **QR code** (top left, about 18 % of the height): changes once per second (`AVSYNC_QR_PERIOD_MS`, default 1000), error correction level M. Payload §5.6. It identifies the test and the event; it is not needed for timing.
2. **Frame counter** (text): the grain index the generator wrote, and in LIVE mode the TAI time of the grain. The text says "frame as written by the generator"; the analyzer reports the frame it saw separately.
3. **Free text**: `AVSYNC_TITLE`, `AVSYNC_SOURCE`, `AVSYNC_NOTES`, and the format, rate, audio format and set offset, drawn once per change.
4. **Timing marker**: a large block (40 % × 40 % of the height, centred). The default audio-timecode signal flashes white (Y 940) for 100 ms from each event, then black (Y 64). Its rising edge coincides with chirp onset at zero source offset. Legacy mode retains alternating black/white per event.
5. **Sweep bar**: a dot moving along a line, reaching a mark at every event and flashing there. A trained engineer sees a gross sync error against the chirp by ear and eye.
6. **Frame counter strip**: 32 blocks along the bottom (each 1/40 of the width, full contrast): the low 24 bits of the sequence number of the current event in Gray code, 4 bits of the frame position within the period, a parity block and three fixed reference blocks (white, black, white) that give the detector its thresholds. It is readable in every frame, survives H.264 at low bitrate and 4:1 downscaling, and is what the analyzer uses to find drops, duplicates and the event id.
7. **Fiducials**: four corner patterns (nested squares) for marker search in a scaled or cropped picture (a multiviewer tile, and the phone app). The live analyzer retains a fast full-raster path and can also locate/rectify an optical or scaled test picture using these fiducials.
8. **TEST SIGNAL** text at the top and bottom, always on, not configurable.

### 5.2 Event timing in the picture

- The marker toggles at the first grain whose origin is at or after `T_k` — exactly at `T_k`, because the plan is on the grain grid.
- The frame counter strip changes its sequence number on the same grain.
- The generator logs and (FILE) records the frame index and media time of every marker change.

### 5.3 Drawing cost

- A static layer (fiducials, labels, frame of the marker, free text) is drawn with Blend2D on a change only, converted to v210 once.
- Per grain the writer copies the static layer and overwrites the marker block, the strip, the sweep dot, the counter texts (pre-rendered digit glyphs) and, once per QR period, the QR block. Target: < 1 ms per 1080p grain and < 0.1 core at 1080p50 (measured in M2).

### 5.4 Video formats

- Generator output: `video/v210`, 1920×1080 at 25, 50 or 59.94 Hz progressive, 1080i25 (top field first), 3840×2160 at 50 Hz, 1280×720 at 50 Hz. Default 1080p50 (`AVSYNC_VIDEO_FORMAT`).
- Analyzer input: `video/v210` and `video/v210a` (key ignored), the same rasters and rates, progressive and interlaced (top field first).

### 5.5 Marker detection

The detector reads only the marker block and the strip:

1. Thresholds from the three reference blocks of the strip (mean luma of black and white; midpoint).
2. Marker state: mean luma of the central 50 % of the marker block against the threshold; `invalid` when the block's 5th-to-95th percentile luma span exceeds the limit (a mixed frame, a transition from a frame-rate converter, a cut). Sparse cursor/hot-pixel defects are excluded from that timing-field span; strip-reference noise keeps its full range check.
3. Strip: each block's mean against the threshold, Gray decode, parity check. A failed parity marks the grain `strip_invalid`.
4. Video event: the first grain whose marker state differs from the previous grain's; its time is the grain's index time.
5. Quality per grain: contrast (white − black), noise (spread inside the blocks), geometry check against the fiducials. Exported as `video_quality` (`good`, `degraded`, `invalid`).
6. Drops and duplicates: from the sequence of strip values against the expected frame positions (a jump = drop, a repeat = duplicate). Counted, reported per event when they fall within ±2 frames of the event (they change that event's uncertainty).

The detector works on 10-bit luma from v210; its cost is a few thousand samples per grain.

### 5.6 QR payload

Compact JSON, versioned, at most 200 bytes:

```json
{"s":"mxl-avsync/1","t":"TEST-2026-0001","m":"LIVE","k":3459211852,"tk":1729673412000000000,"o":0,"r":2,"f":"1080p50"}
```

| Key | Meaning |
| --- | --- |
| `s` | schema, `mxl-avsync/1` |
| `t` | test id (`AVSYNC_TEST_ID`, default the generator's NMOS seed) |
| `m` | `FILE` or `LIVE` |
| `k` | sequence number of the next event |
| `tk` | that event's set video time: TAI ns (LIVE) or ms on the file timeline (FILE) |
| `o` | expected AV offset in ms (or `"sweep"`, the manifest then holds the values) |
| `r` | event rate |
| `f` | video format token |

The long field names of the draft (`schema`, `test_id`, …) are used in the manifest and the results; the QR uses the short keys to stay small and robust. The JSON schema is `schemas/qr-payload.schema.json`.

---

## 6. Test audio

### 6.1 Signal

- Four chirp shapes, all band-limited, Hann-windowed (10 % fades), duration `AVSYNC_CHIRP_MS` (default 30 ms), band `AVSYNC_CHIRP_LOW_HZ`–`AVSYNC_CHIRP_HIGH_HZ` (default 700–2500 Hz, inside the G.711 telephone band, so the file-mode G.711 test aid can still carry them):
  - `0` linear up-chirp, `1` linear down-chirp, `2` up-chirp in the lower 60 % of the band, `3` down-chirp in the upper 60 % of the band.
  The four are chosen for low cross-correlation; M0 measures it and MAY change the shapes before 1.0.
- The default is compact audio timecode v3: a known up-chirp references its start and a protected voice-band data burst independently carries source, common video-event time and audio offset. See [wire format, timing, compatibility and tests](docs/audio-timecode.md). Legacy mode retains centre reference and chirp `k mod 4`.
- Silence between chirps (no noise bed, no tone).
- Level `AVSYNC_CHIRP_LEVEL_DBFS` (default −12 dBFS peak), never clipping.
- Every output sample is computed from its absolute sample index (a pure function), so a restart, a block size or a late block never shifts the signal, and the chirp centre can sit between two samples.
- All channels carry the same signal by default. `AVSYNC_AUDIO_CHANNELS` (1–16, default 2). A per-channel sequence for a channel-swap test is stage 4.
- MXL: `audio/float32`, 48 kHz. FILE: PCM 24-bit 48 kHz by default, other rates by resampling in the generator (44.1 kHz and 96 kHz in the tests).

These are starting values, not proven optima. M0 tests them against level change, noise, AGC, filtering, resampling and a G.711 table codec (§14.1) and records the result in the plan. That G.711 path is a test aid, not a live SIP input.

### 6.2 Detection

- Matched filter: normalised cross-correlation of the input with each of the four reference chirps (FFT based, overlap-save), on one channel (`AVSYNC_AUDIO_CHANNEL`, default 0).
- A detection needs: the normalised peak above `AVSYNC_AUDIO_THRESHOLD` (default 0.5, from M0), a peak-to-sidelobe ratio above a limit, and one chirp shape clearly better than the other three. Normalisation makes it level-independent; the energy floor keeps noise from becoming a hit.
- Time: the peak position refined by parabolic interpolation (sub-sample).
- Per detection: time, chirp id, correlation (0–1), peak level in dBFS, clip indication (samples at ±1.0), quality (`good`, `degraded`, `invalid`).
- A second peak of the same chirp within one period = `duplicate`; an expected chirp not found within ±2 periods = `missing`; two shapes nearly equal = `ambiguous`. Each is counted and reported, never silently dropped.

### 6.3 Safety

See §11: the chirps are audible test tones and are muted by one control.

---

## 7. Media function

### 7.1 Generator — NMOS and flows

| Resource | Label | Group hint |
| --- | --- | --- |
| Node | `NMOS_LABEL` verbatim, or `HOST_ID` when unset; no role suffix | — |
| Device | Node label + ` generator` | — |
| Video source, flow, sender | `AVSync Test Video` | `AVSync Test:Video` |
| Audio source, flow, sender | `AVSync Test Audio` | `AVSync Test:Audio` |

- Senders, flows and the device carry the tag `urn:x-mxl:tag:test-signal/v1.0` = `["mxl-avsync"]` (`TODO_` confirm the tag namespace with the platform). Video flow `description`: `TEST SIGNAL — AV sync, <rate>/s, offset <n> ms`. Sender and receiver `interface_bindings` name the host interface. Node `hostname` is `HOST_ID` with any domain suffix removed.
- Flows are written in the generator's own output domain (G2). History `MXL_HISTORY_DURATION_NS` (default 200 ms).
- A change of format, channel count or rate mints new flow ids (flow token, as mxl-scopes); the senders follow.

### 7.2 Analyzer — NMOS and flows

| Resource | Label | Group hint |
| --- | --- | --- |
| Device | Node label + ` analyzer` | — |
| Video receiver | `AVSync In Video` | `AVSync In:Video` |
| Audio receiver | `AVSync In Audio` | `AVSync In:Audio` |
| Video source, flow, sender | `AVSync Result Video` | `AVSync Result:Video` |

- Receiver caps (BCP-004-01): video as §5.4; audio `audio/float32`, 48 kHz, 1–64 channels.
- IS-05 behaviour as mxl-scopes §6: an activation is accepted even if the flow is not there yet (`waiting`, backoff 250 ms → 5 s); `master_enable: false` stops the reader (`not_routed`); the active routes are saved in `<AVSYNC_STATE_DIR>/routes.json` and restored at start, into the readers and into IS-05 and the IS-04 subscriptions.
- **Recreated flows:** a writer that restarts recreates its flow with the same id. Each reader MUST detect it (the data file changed, `MXL_ERR_FLOW_INVALID`, or the head stuck for ≥ 1 s) and reopen with a fresh MXL instance, releasing the old reader first (the mxl-st2110-gateway and mxl-srt-gateway lesson). No stale mapping of a deleted flow.
- Each grain's size is checked before it is read (the multiviewer 1080i lesson).

### 7.3 Result video flow

- `video/v210`, `AVSYNC_RESULT_FORMAT` (default 960×540) at `AVSYNC_RESULT_RATE` (default 25), for a multiviewer tile.
- Shows: the AV offset in large digits with the sign and the sentence (§3.2), the grade colour (§8.3), the latencies when a reference exists, a trend of the last 60 s, the input state, and the label of the routed senders. While nothing is measured: `NO SIGNAL`, `WAITING`, `NOT ROUTED`, `NO EVENTS`, never an old number without its age.
- Redrawn at 5 Hz, written at the output rate (the mxl-scopes writer pattern).
- `AVSYNC_RESULT_ENABLE=false` removes the sender and flow.

### 7.4 One measuring point per instance

The rule of mxl-scopes §6, for the same reasons: a stuck reader takes down one measurement; Kubernetes sizes one predictable budget; one NMOS receiver pair per node; several points are several instances, and the multiviewer shows their result flows side by side. The cost is one NMOS node and one web endpoint per point.

---

## 8. Results, grading and statistics

### 8.1 Per event

| Field | Notes |
| --- | --- |
| `test_id`, `event_id`, `seq`, `mode` | |
| `video_event_time`, `audio_event_time` | ms (FILE) or TAI ns (LIVE), index times |
| `av_offset_ms`, `expected_av_offset_ms`, `deviation_ms` | §3.1 |
| `av_offset_arrival_ms` | LIVE only |
| `video_latency_ms`, `audio_latency_ms`, `video_index_shift_ms`, `audio_index_shift_ms` | LIVE with a reference only; else absent |
| `grade` | `pass`, `warn`, `fail`, `invalid` (§8.3) |
| `uncertainty_ms` or `uncertainty_status: "unknown"` | §8.4 |
| `video_quality`, `audio_quality`, `correlation`, `audio_level_dbfs`, `clipped` | |
| `flags` | `video_drop_near`, `video_dup_near`, `strip_invalid`, `qr_mismatch`, `ambiguous`, `out_of_range`, `frame_rate_converted`, `batch_commit` |
| `reference` | §3.3 |
| `stream` | format, rate, channels, the routed sender labels (LIVE) or the container and codec (FILE) |

A missing audio or video event gives a record with `grade: "invalid"`, the reason, and no offset.

### 8.2 Series statistics

Over a window (`AVSYNC_STATS_WINDOW`, default the last 120 events; FILE: the whole file) and since start:

- valid and discarded counts, with the reasons;
- min, max, median, mean, standard deviation of the AV offset and the deviation (and the latencies when present);
- p5, p95, p99 only with at least 20, 20 and 100 valid events; below that the field is `null` with `insufficient_samples`;
- the time series of the offset;
- dropouts (missing events) and outliers (more than 3 × the median absolute deviation from the median) listed separately. Outliers are never removed silently: they stay in the raw data with their reason, and the statistics are given with and without them.

### 8.3 Grading

The grade compares the **AV offset** (index time) with three windows (the maintainer's decision, §18):

| Grade | Window (positive = audio late) | Meaning |
| --- | --- | --- |
| **pass** (green) | −5 ms … +15 ms | the accuracy a single stage of the chain should keep |
| **warn** (orange) | −40 ms … +60 ms, outside pass | inside EBU R37 end to end, but more than a stage should add |
| **fail** (red) | outside −40 … +60 ms | outside EBU R37 |
| **invalid** (grey) | — | no valid pair |

- Settings: `AVSYNC_PASS_MIN_MS` (−5), `AVSYNC_PASS_MAX_MS` (15), `AVSYNC_WARN_MIN_MS` (−40), `AVSYNC_WARN_MAX_MS` (60). Validation: `warn_min ≤ pass_min ≤ 0 ≤ pass_max ≤ warn_max`, else exit 78.
- With an expected offset other than 0 (a deliberate test), the windows apply to the **deviation** instead, and the result says `graded_on: "deviation"`.
- Each event is graded. The **current grade** (UI, widget, result flow, metric) is the grade of the median of the last `AVSYNC_GRADE_EVENTS` valid events (default 10), so one outlier does not flash red. The grade becomes `invalid` after `AVSYNC_STALE_S` (default 5 s) without a valid event.
- A boundary value belongs to the better grade (−5.0 is pass, +60.0 is warn).

### 8.4 Uncertainty

`uncertainty_ms` is an interval half-width built from known parts, never a guess:

- audio: the detector's timing uncertainty at the measured correlation (a table from M0's tests);
- video: 0 on a path without frame-rate conversion and without a drop or duplicate near the event; else one input frame (or field);
- arrival times: the reader's wake-up resolution plus the largest commit batch seen on that flow;
- a reference with `sync_state: unknown` makes latencies `uncertainty_status: "unknown"` (the offset is unaffected).

If a part is unknown the result says `uncertainty_status: "unknown"` rather than a number.

### 8.5 Several measuring points

End-to-end latency between two analyzers is `t_arr(destination) − t_arr(source)` for the same `event_id`, valid only when both report `reference.type = "tai-ptp"`. In 1.0 each analyzer already reports latency against the generator's `T_k`, which covers the generator-to-point case. Merging two analyzers' event logs (`mxl-avsync merge a.json b.json`, and the same merge of saved results over REST) is a measurement in this repo. Moving the flows between nodes is `mxl-fabrics-agent`, not code here.

---

## 9. File generator and file analyzer

### 9.1 Generator CLI

```
mxl-avsync generate --format 1080p50 --duration 60 --offset 0 \
  [--offset-sweep -40,-20,0,20,40] [--event-rate 2] [--audio-timecode 1200|600|legacy] [--audio-rate 48000] [--channels 2] \
  [--container mov|mp4|mkv] [--video-codec prores|v210|h264] [--audio-codec pcm24|pcm16|aac] \
  [--title …] [--notes …] --out test.mov
```

- Writes the file and `test.mov.manifest.json` next to it.
- Default container and codecs: **MOV with ProRes 422 HQ and PCM 24-bit 48 kHz** — readable by the usual NLEs, DAWs and players, with exact time bases (video 1/50, audio 1/48000), no encoder delay in either essence. Matroska's default 1 ms timestamp scale and AAC's encoder delay (priming samples) are exactly the traps this tool must expose, so they are offered as variants and tested, not used as the default. M1 confirms the choice with round-trip tests and records it in the plan.
- The file is labelled as test material in the container metadata (`title`, `comment`: `TEST SIGNAL — mxl-avsync`) and in the picture.
- FFmpeg libraries (`libavformat`, `libavcodec`, `libswresample`) do the encoding and muxing; no own codecs.

### 9.2 Manifest

`schemas/manifest.schema.json`, `"schema": "mxl-avsync-manifest/1"`:

```json
{
  "schema": "mxl-avsync-manifest/1",
  "generator": {"version": "1.0.0", "core": "1.0.0"},
  "test_id": "TEST-2026-0001",
  "mode": "FILE",
  "timeline": "file",
  "duration_ms": 60000,
  "video": {"width": 1920, "height": 1080, "fps_num": 50, "fps_den": 1, "scan": "progressive", "codec": "prores_hq"},
  "audio": {"codec": "pcm_s24le", "sample_rate_hz": 48000, "channels": 2, "chirp": {"ms": 30, "low_hz": 700, "high_hz": 2500, "level_dbfs": -12}},
  "container": {"format": "mov", "video_time_base": "1/50", "audio_time_base": "1/48000", "audio_start_pts": 0, "video_start_pts": 0},
  "events": [
    {"event_id": "EVT-000001", "seq": 1, "chirp_id": 1, "video_frame": 50, "video_timeline_ms": 1000, "audio_sample": 48000, "audio_timeline_ms": 1000.0, "expected_av_offset_ms": 0, "rounding_residual_us": 0}
  ]
}
```

### 9.3 Checking the written file

After writing, the generator re-opens the file with FFmpeg and checks each stream's start time, time base, edit list and the real PTS of every event frame and the audio sample positions against the plan. Differences are written into the manifest (`container.checks`) and fail the command (exit 1) unless `--allow-container-shift`. This is the draft's "real timestamps in the artefact, not rounded QR values".

### 9.4 Analyzer CLI

```
mxl-avsync analyze <file> [--manifest m.json] [--json out.json] [--from s] [--to s]
mxl-avsync validate <file.json>          # checks a manifest or a result against its schema
mxl-avsync merge <a.json> <b.json>       # end-to-end between two measuring points (§8.5)
```

- Decodes video and audio with FFmpeg, keeping the stream PTS (no re-timing), runs the same core detectors as the live analyzer, pairs, grades and prints a human report (§9.5) and a JSON result (`schemas/result.schema.json`).
- Without a manifest it takes the expected offset from the QR codes. With one, it also compares the found events against the plan and reports missing or extra events and timestamp shifts.
- Exit 0 when the analysis ran (whatever the grade), 2 when no event was found, 78 on a bad argument. `--fail-on warn|fail` makes the exit code follow the grade (for CI).

### 9.5 Human report

```
mxl-avsync analyze rec.mov
  test TEST-2026-0001 · FILE · 1080p50 · pcm 48 kHz 2 ch · 118 events
  AV offset   median +35.2 ms (audio 35 ms after video)   p5 +34.9  p95 +35.6  σ 0.2
  expected    +0 ms → deviation +35.2 ms                   grade WARN (−40…+60)
  events      118 valid · 0 missing · 0 duplicate · 0 ambiguous · 0 outliers
  video       0 drops · 0 duplicates · quality good
  audio       correlation 0.97 · level −12.1 dBFS · no clipping
  uncertainty ±0.1 ms (audio) ±0 frames (video)
```

---

## 10. Configuration

Precedence: environment > `AVSYNC_CONFIG_FILE` (flat JSON, keys are the variable names, string values) > defaults. Invalid configuration exits 78. The file is written atomically. "Live" keys apply without a restart, including a key that was set in the environment at start: `PATCH /api/v1/generator` and `PUT /api/v1/config` change it and the next events use the new value. Import still skips env-set keys and lists them. "Flow" keys apply live and mint new output flow ids; "restart" keys are saved and reported as `restart_required`. The CLI takes the same keys as `--` options.

### 10.1 Both roles

| Key | Default | Apply | Meaning |
| --- | --- | --- | --- |
| `AVSYNC_ROLE` | — (required for `serve`) | restart | `generator` or `analyzer` |
| `AVSYNC_EVENT_RATE` | 2 | flow (gen) / live (ana) | 1 or 2/s for timecode; legacy also supports 4/s. 600 baud requires 1/s. Analyzer adopts QR rate |
| `AVSYNC_AUDIO_TIMECODE` | 1200 | flow | `1200`, `600`, or `legacy`; timecode has fixed 30-ms / 700–2500-Hz prefix, onset reference and one white pulse/event |
| `AVSYNC_CHIRP_MS` | 30 | flow / live | 10–100 |
| `AVSYNC_CHIRP_LOW_HZ`, `AVSYNC_CHIRP_HIGH_HZ` | 700, 2500 | flow / live | band, 100–20000, low < high |
| `AVSYNC_STATE_DIR` | `/config` | restart | `config.json`, `routes.json`, `events/` |
| `AVSYNC_CONFIG_FILE` | empty | restart | empty uses `<AVSYNC_STATE_DIR>/config.json` |

### 10.2 Generator

| Key | Default | Apply | Meaning |
| --- | --- | --- | --- |
| `AVSYNC_VIDEO_FORMAT` | `1080p50` | flow | §5.4 |
| `AVSYNC_AUDIO_CHANNELS` | 2 | flow | 1–16 |
| `AVSYNC_AV_OFFSET_MS` | 0 | live | −500…+500, set offset |
| `AVSYNC_CHIRP_LEVEL_DBFS` | −12 | live | −40…−1 |
| `AVSYNC_AUDIO_ENABLE` | `true` | live | `false` mutes the chirps (silence, flows keep running) |
| `AVSYNC_AUDIO_LEAD_MS` | 20 | restart | how far ahead of TAI audio is written |
| `AVSYNC_QR_PERIOD_MS` | 1000 | live | 500–5000 |
| `AVSYNC_TEST_ID` | the NMOS seed | live | in the QR and the results |
| `AVSYNC_TITLE`, `AVSYNC_SOURCE`, `AVSYNC_NOTES` | empty | live | free text on the picture (each ≤ 80 characters) |

### 10.3 Analyzer

| Key | Default | Apply | Meaning |
| --- | --- | --- | --- |
| `AVSYNC_AUDIO_CHANNEL` | 0 | live | channel the detector listens to |
| `AVSYNC_AUDIO_WINDOW_MS` | 10 | restart | reader window |
| `AVSYNC_AUDIO_THRESHOLD` | 0.5 (`TODO_` from M0) | live | normalised correlation needed |
| `AVSYNC_QR_RATE` | 2 | live | QR decodes per second, 0 = off |
| `AVSYNC_PASS_MIN_MS`, `AVSYNC_PASS_MAX_MS` | −5, 15 | live | §8.3 |
| `AVSYNC_WARN_MIN_MS`, `AVSYNC_WARN_MAX_MS` | −40, 60 | live | §8.3 |
| `AVSYNC_GRADE_EVENTS` | 10 | live | events behind the current grade |
| `AVSYNC_STALE_S` | 5 | live | grade `invalid` after this long without a valid event |
| `AVSYNC_STATS_WINDOW` | 120 | live | events in the window statistics |
| `AVSYNC_EVENT_LOG` | `true` | live | keep the event records in `<state>/events/` (rotated, 7 days or 100 MB) |
| `AVSYNC_RESULT_ENABLE` | `true` | flow | result video flow (§7.3) |
| `AVSYNC_RESULT_FORMAT` | `960x540` | flow | even width and height, ≤ 1920×1080 |
| `AVSYNC_RESULT_RATE` | 25 | flow | rate token |
| `AVSYNC_HOLD_MS` | 1000 | live | `no_signal` after this long without a new grain |
| `AVSYNC_CLOCK_MAX_ERROR_US` | 100 | restart | Maximum reported kernel error or absolute ptp4l master offset (µs) for `locked`; unreported kernel defaults do not supply accuracy |
| `AVSYNC_PTP_SOCKET` | empty | restart | Optional ptp4l management socket, e.g. `/var/run/ptp4l`; requires host networking and readable/writable socket permissions, with a read-only socket mount (§3.3) |
| `AVSYNC_PTP_STATUS_URL` | empty | restart | Explicit override of socket/kernel clock selection: `http://` JSON `{"sync_state","accuracy_ns"}`. Latency is filled only when `sync_state` is `locked` and an arrival time was measured |

### 10.4 Platform settings (same names and meaning as the siblings)

| Key | Default | Apply |
| --- | --- | --- |
| `HOST_ID` | hostname | restart |
| `MXL_DOMAIN_SCAN_PATH` | `/Volumes/mxl` | restart |
| `MXL_OUTPUT_DOMAIN_DIR` | `/Volumes/mxl/avsync-<seed-short>` | restart |
| `MXL_OUTPUT_DOMAIN_ID` | UUIDv5 of the seed | restart |
| `MXL_HISTORY_DURATION_NS` | 200000000 | restart (new domain only) |
| `MXL_CLEANUP_ON_EXIT` | `false` | restart |
| `NMOS_ENABLE` | `true` | restart |
| `NMOS_REGISTRY_ADDRESS` / `NMOS_REGISTRY_PORT` | empty / 3210 | restart |
| `NMOS_QUERY_ADDRESS` / `NMOS_QUERY_PORT` | registry address / registry port + 1 | restart |
| `NMOS_DNS_SD` | `false` | restart |
| `NMOS_PORT` | 3332 (+3333 reserved) | restart |
| `NMOS_SEED` | `<HOST_ID>-avsync` | restart |
| `NMOS_LABEL` | empty (`HOST_ID`) | restart |
| `NMOS_HOST_ADDRESS` | first non-loopback IPv4 | restart |
| `NMOS_TAGS` | `{}` | restart |
| `WEB_ENABLE` | `true` | restart |
| `WEB_PORT` | 8180 | restart |
| `WIDGET_FRAME_ANCESTORS` | `'self'` | restart |
| `LOG_LEVEL` | `info` | live |
| `LOG_FORMAT` | `json` | restart |
| `SHUTDOWN_TIMEOUT_S` | 10 | restart |

No MediaMTX and no preview stream: the generator's picture is seen through any MXL monitor, the analyzer's result is a number and its result flow. Ports MUST NOT collide; a port that cannot be bound exits 75.

---

## 11. Safety of test signals

- The picture always shows TEST SIGNAL (top and bottom); the generator's NMOS labels start with `AVSync Test`, its flows carry the test-signal tag and description (§7.1).
- The function **never** issues IS-05 requests to other devices and never changes a route. It is routed like any other function, by the production or an operator.
- The web UI shows a green **Test signal active** status pill beside the other header pills while the generator writes audible chirps; muted/stopped states are neutral. The widget retains its TEST SIGNAL ACTIVE indicator. Both provide a **mute** button (`AVSYNC_AUDIO_ENABLE`). Mute is live and takes effect at the next audio block (≤ 10 ms).
- `POST /api/v1/generator/stop` writes black and silence with the TEST SIGNAL text kept (the flows stay, so nothing downstream breaks); `/start` resumes.
- Test files carry the test label in their metadata and picture (§9.1).
- The platform SHOULD place generators only in test productions (`TODO_` the platform decides; noted in §16).

---

## 12. Phone app (stage 3, the last milestone)

Not built before 1.0 of the media function is released and its detectors are validated. The architecture is fixed now so the core fits.

### 12.1 Function

At a real monitor and loudspeaker, the phone's camera sees the test picture and its microphone hears the chirps. The app shows:

- `AV offset: +35 ms` and `Audio comes 35 ms after video`;
- the grade colour with the same windows as §8.3 (from the QR's test id the app MAY fetch the analyzer's windows; else the defaults);
- `Quality: good / degraded / invalid` and `Calibration: valid / missing / expired`;
- a time series and the statistics after several events;
- saved measurements (local), compare, export as the result JSON of §8 (`mode: "LIVE"`, `reference.type: "none"`). The last valid measurement remains saveable through temporary recognition gaps, explicitly labelled with age/spread; a retained sample is never relabelled as a fresh measurement. Saving preserves its original acquisition time plus save time and age. Source changes and explicit clears discard the retained sample. Outline smoothing/brief hold affect presentation only and must not feed back into detection or timestamping.

Compact phone event timing retains the first clear black-to-white optical onset while the Gray code is confirmed. A code outside the known QR identity window cannot acquire lock. A missed event may be skipped only when a bounded sequence gap agrees with the optical time interval; no observation is synthesized for the missing event.

AV offset is measured. Optional estimated latency uses the phone clock with explicitly unknown physical timing accuracy (§3.3). The Generate tab also supplies a native test source for an external camera/analyzer; its source-output calibration is separate from capture calibration.

### 12.2 Technology

- **Expo** (React Native) for the UI, one code base for iOS and Android, with an **Expo native module** per platform for capture and timing.
- The detectors are `avsync-core` (C++20, no dependencies), compiled into the native module (iOS: Objective-C++; Android: JNI via the NDK). The JavaScript side only shows results.
- Capture: iOS `AVCaptureSession` (video frames with presentation timestamps on the host clock) and `AVAudioEngine`/`AVAudioSession` input with host-time stamps; Android Camera2/CameraX (sensor timestamps, `SENSOR_INFO_TIMESTAMP_SOURCE` must be `REALTIME`) and AAudio/Oboe input timestamps on `CLOCK_MONOTONIC`. Both platforms MUST put video and audio on one clock; a device that cannot is reported as unsupported.
- Video at the camera's frame rate (≥ 60 fps where the device offers it). The marker search uses the fiducials (§5.1) to find the picture in the camera image; rolling shutter is handled by reading the marker block's row time (the row's offset within the frame's readout).
- M-P0 (§15 plan) decides whether this stays Expo with native modules or moves to native apps; precise capture timestamps matter more than code reuse (the draft's rule).

### 12.3 Calibration

The camera and microphone have different capture latencies (OS buffers, exposure, rolling shutter, audio timestamping). They are not assumed to be in sync.

- **Calibration rig:** a small reference device that drives an LED and a piezo click from one microcontroller pin at the same instant (well under 0.1 ms apart), pulsing at the event rate. It is the independent reference the draft asks for; a phone measuring its own playback is not one. `TODO_` build it in M-P1 (parts list and firmware in `mobile-app/calibration/`).
- The app measures the rig, stores the capture offset per device model and OS version with the date, the method, the number of events and the spread, and subtracts it from later measurements.
- A calibration expires after an OS update or 90 days. Uncalibrated measurements are allowed and shown as **uncalibrated** in orange, with no grade colour.
- Expected accuracy is unknown until measured on real devices; the app shows the measured spread of its own calibration as its uncertainty.

---

## 13. Signal paths and test matrix

Each path this repo supports has test results and known limits in `docs/test-matrix.md`. This repo does not implement a transport. File-mode H.264 and G.711 PCMU/PCMA are test aids only: FFmpeg filters in CI, so the detectors can be tried on a compressed picture and on telephone-band audio. They are not live inputs and they do not stand in for a gateway.

The ST 2110 hour run is not code here. It goes through `mxl-st2110-gateway`: avsync generator → MXL → gateway → network → gateway → MXL → avsync analyzer, 1080p50 and 1080i25, one hour, with index shift and latency reported when that lab is run. SRT arrives as an MXL flow from `mxl-srt-gateway`. SIP/G.711 live audio arrives as an MXL flow from the sibling gateway that speaks SIP. Cross-node movement is `mxl-fabrics-agent`. Stage 2 is: use those sibling gateways and test the matrix. No transport code is added to this repo.

| Path | Stage | How it is tested |
| --- | --- | --- |
| PCM reference (file round trip) | 1 | CI: generate → analyze; 0, +35, −35 ms and a sweep; inserted delays |
| MXL reference (generator → analyzer, one node) | 1 | CI with real MXL in a temp root; lab |
| File-mode test aids: H.264 (several bitrates incl. 1 Mbit/s), scaling 1080→720→540, frame-rate conversion 50→25 and 50→59.94, resampling 48→44.1 kHz, level −20 dB, AGC (a simulated compressor), G.711 PCMU and PCMA | 1 | CI with FFmpeg filters. Test aids only, not live SRT or SIP |
| Controlled delay (analyzer's test tool shifts audio or video by n ms) | 1 | CI and lab |
| Frame drops and duplicates (test tool) | 1 | CI |
| Multiviewer tile (scaled, cropped) | — | fiducial search in this tree (`analyze --locate`); a lab tile is not a transport |
| Phone at a monitor | 3 | real devices with the calibration rig |

---

## 14. Testing

### 14.1 Unit (doctest, `avsync-core`, no libmxl, no FFmpeg)

- Event plan: ids, times on the grain grid at 25/50/59.94p and 1080i25, the expected offset and its rounding, a sweep, determinism (same config → same plan bytes).
- Chirps: duration, band (spectrum), level, no clipping, start and centre time, sample-index purity (a block split anywhere gives the same samples).
- Chirp detection, with a measured timing error and detection rate for each condition: clean; level −40…0 dBFS; white noise at SNR 20, 10, 0 dB; a compressor/AGC; band-pass 300–3400 Hz; resampling to 44.1 and back; G.711 μ-law and A-law (a table-codec test aid, not a live SIP input); a 10 ms gap; two chirps close together; chirp-id cross-confusion. The results become the uncertainty table of §8.4 and the default threshold.
- Marker detection on synthetic frames: clean, H.264-like blocking (quantised DCT), 4:1 downscale, blur, noise, a half-transition frame (`invalid`), wrong geometry; Gray code and parity; drops and duplicates.
- QR payload against the schema; size ≤ 200 bytes.
- Sign convention: audio late → positive; audio early → negative (also in every UI and metric test).
- Pairing: correct ids up to ±2 periods; `out_of_range` beyond; missing, duplicate and ambiguous counted.
- Grading: window edges (−5.0 pass, +60.0 warn, +60.1 fail), the deviation rule, the median-of-N current grade, stale → invalid.
- Statistics: percentiles withheld below their minimum counts; outliers reported, not removed.
- Config precedence, validation (78), UUIDv5 ids, ports, widget list and pages (params, CSP, CORS, no `X-Frame-Options`).

### 14.2 Integration (CI)

- `tests/integration/file.sh`: generate MOV (and the MP4/MKV/AAC variants) with offsets 0, +35, −35 and a sweep; analyze; every event found; deviation within the measured uncertainty; container checks pass (and AAC's priming is reported, not hidden); the transcode set of §13 with its results written to `docs/test-matrix.md` by the test.
- `tests/integration/live.sh`: real MXL in a temp root, generator and analyzer, registry stand-in, IS-05 activation; `/readyz` 200; offset 0 then a live change to +40 (warn) and +80 (fail) through `PATCH`; the result flow exists and its pixels show the grade colour; the test tool adds 20 ms to the audio flow → offset +20; a generator restart (flow recreated) → the analyzer reopens and measures again; mute → `missing` audio, grade `invalid` after `AVSYNC_STALE_S`; routes restored after an analyzer restart; `/metrics` has every family at 0 from the start; config export and import; SIGTERM exits 143, the nodes leave the Query API, domains removed with `MXL_CLEANUP_ON_EXIT=true`.
- `tests/nmos/amwa.sh`: IS-04-01, IS-05-01, IS-05-02, BCP-007-03-01 (outside the default job).

### 14.3 Lab (hardware, not CI)

- MXL reference path 1 h at 1080p50 and 1080i25: offset 0 ± audio resolution, 0 invalid events, latency stable; CPU of both roles.
- The ST 2110 hour run is not a lab of this repo. It goes through `mxl-st2110-gateway` as in §13.
- Result flow on a multiviewer tile; the widget on an operator screen.
- Alerts fire in Grafana on a forced +80 ms offset and on a stopped generator.
- Production up and down on the platform with one generator and two analyzers.

---

## 15. Technology and build

### 15.1 Choices (the draft's §12 and §17 questions 1–4)

| Area | Choice | Why |
| --- | --- | --- |
| Language | C++20 (decided, §18) | the siblings' toolchain (MXL C API, nmos-cpp, Blend2D); a dependency-free core compiles into the phone app's native module |
| Media library | FFmpeg libraries (system, Ubuntu 24.04) for files only | exact PTS control, ProRes/H.264/AAC/PCM, the transcode tests; no GStreamer (siblings' rule) |
| Live I/O | MXL C API at the platform pin | no media library in the live path |
| QR encode | Nayuki QR Code generator (MIT, one file, vendored) | small, no dependencies, also in the phone build |
| QR decode | zxing-cpp (Apache-2.0) | robust on blurred and scaled codes; also builds for iOS and Android |
| FFT / correlation | pocketfft (BSD-3, header only, vendored) | no dependencies, double and float |
| Marker detection | own code in `avsync-core` (block means, Gray decode) | trivial maths; no OpenCV needed in 1.0. Fiducial search for a scaled picture MAY add a small dependency |
| Text and drawing | Blend2D with DejaVu Sans compiled in (versions of mxl-multiviewer) | the family's choice |
| CLI | own argument parser on top of the config layer (same keys) | one source of truth for settings |
| Schemas | JSON Schema draft 2020-12 in `schemas/`, validated in tests (nlohmann/json + json-schema-validator, MIT) | the draft's versioned JSON requirement |
| Web UI | Vue 3, one embedded HTML file, family look, no CDN | as the siblings |
| Tests | doctest (vendored); shell integration tests | as the siblings |
| Phone | Expo with native modules (decided, §18), final check in M-P0 | §12.2 |

Licences: MIT (this repo, decided), Apache-2.0 (zxing-cpp), BSD-3 (pocketfft), Zlib (Blend2D), LGPL (FFmpeg, dynamically linked), MIT (Nayuki, nlohmann). Checked again at 1.0 in `NOTICE`.

### 15.2 Pins

- MXL: `dmf-mxl/mxl` `release/v1.1` at `218ddaa0a08c12ffe75fc475ae65aa3d9eef16d7`, `-DMXL_ENABLE_FABRICS_OFI=OFF`, one `MXL_REF` in `docker/Dockerfile` and CI. Public C API only.
- nmos-cpp: the siblings' commit (`fe303849527394b03bdedc8f161f377fe458bb62`).
- C++20, CMake ≥ 3.24, Ninja, GCC ≥ 12 or Clang ≥ 16. Base image Ubuntu 24.04; starts on any x86-64 CPU without a GPU.

### 15.3 Repository layout

```
mxl-avsync/
  AGENTS.md  CHANGELOG.md  IMPLEMENTATION_PLAN.md  README.md  SPECIFICATION.md  LICENSE  NOTICE
  schemas/            qr-payload, manifest, result, event-log (JSON Schema)
  core/               avsync-core: event plan, chirps, audio detector, marker draw/detect,
                      pairing, grading, statistics, uncertainty (no dependencies)
  generator/          picture composition (Blend2D), QR, file writer (FFmpeg), MXL writers
  analyzer/           file reader (FFmpeg), MXL readers, result picture
  app/                main, config, CLI, roles
  nmos/  ops/         NMOS node; HTTP, WebSocket, REST, widgets, metrics
  web/                Vue 3 UI and the avsync widget
  mobile-app/         stage 3: Expo app, native modules, calibration rig
  tests/  unit/ integration/ fixtures/ tools/ (delay and drop injector, pattern checks) nmos/
  docs/   architecture.md  timing-model.md  calibration.md  test-matrix.md  performance.md
          draft-2026-10-23-de.md
  docker/  deploy/  .github/workflows/
```

`avsync-core` builds alone (`-DAVSYNC_CORE_ONLY=ON`), so its tests run anywhere and the phone build takes it as is.

---

## 16. Web UI, REST, widget, metrics, alerts

### 16.1 Web UI

Family look: header with label, role, state and registration pills, version; tabs with their own address.

- Generator: **Signal** (format, rate, offset, chirp level, mute, stop/start, free texts; the header shows the test-signal status pill), **NMOS**, **Status**, **Settings**.
- Analyzer: **Measurement** (the offset in large digits with sign and sentence, grade colour, latencies, trend, statistics, the last events with flags), **Input** (state, routed senders, formats, counters, drops, duplicates), **Events** (the event log, download as JSON), **NMOS**, **Status**, **Settings** (windows, thresholds; env-set keys read-only; export and import).

### 16.2 REST

| Method | Path | Role | Purpose |
| --- | --- | --- | --- |
| GET | `/api/v1/info` | both | version, core version, MXL revision, role, label |
| GET | `/api/v1/status` (also `/statusz`) | both | readers or writers, NMOS summary |
| GET/PATCH | `/api/v1/generator` | gen | offset, level, mute, texts, rate |
| POST | `/api/v1/generator/stop`, `/start` | gen | §11 |
| GET | `/api/v1/measurement` | ana | current grade, offset, latencies, window statistics |
| GET | `/api/v1/events?since=<seq>&limit=` | ana | the newest matching event records, newest first, plus `total` |
| GET | `/api/v1/statistics` | ana | window and since-start statistics (§8.2) |
| POST | `/api/v1/statistics/reset` | ana | new statistics epoch (the event log keeps everything) |
| GET/PATCH | `/api/v1/grading` | ana | the four window edges, `AVSYNC_GRADE_EVENTS` |
| GET | `/api/v1/events/ws` | both | WebSocket: state, settings, each event as it is paired |
| GET | `/widgets`, `/widget/{id}` | both | §16.3 |
| GET/PUT | `/api/v1/config` | both | flat key update; `restart_required`. `X-Env-Set` lists keys that came from the environment |
| GET | `/api/v1/config/export` | both | `version`, `secrets: false`, `settings`, `routes` |
| POST | `/api/v1/config/import` | both | env-set keys are skipped and listed |
| GET | `/api/v1/config/env` | both | `KEY=value` text |

### 16.3 Widgets

The widget contract of Appendix B.2.

- Analyzer **`avsync`**: params `latency` (boolean, default true), `trend` (boolean, default true); `min_size` 240×120. The offset with sign, the sentence, the grade colour as the background band, the latency when present, a 60 s trend. Grey with the reason when invalid.
- Generator **`generator`**: params none; `min_size` 240×80. TEST SIGNAL ACTIVE, mute and stop buttons, the set offset.
- `/widget` routes: `Content-Security-Policy: frame-ancestors <WIDGET_FRAME_ANCESTORS>`, no `X-Frame-Options`; `widget-ready` and `widget-size` messages; 400 on a bad parameter, 404 on an unknown widget.

### 16.4 Probes

- `/livez`: 200 while the media threads' heartbeats are younger than 5 s. A generator that finds a leftover output domain whose id is not its own returns 503. Set `MXL_OUTPUT_DOMAIN_ID` or `NMOS_SEED` to keep that id, or remove the leftover directory. The process does not delete a domain with a different id.
- `/readyz`: 200 when the process and HTTP service are up and, with a registry and `NMOS_ENABLE=true`, the node is listed in the Query API. The generator additionally requires its video and audio output flows to be created. Analyzer readiness never depends on routing, input flows or signal: `not_routed`, `waiting` and `no_signal` are normal ready states. Their media health remains visible in `/statusz`, `/api/v1/status`, input-state metrics and the UI. Failed process health still returns 503.

### 16.5 Metrics

Prefix `mxl_avsync_`. Every family is exported from the start, counters at 0 (dashboards show "no data" otherwise). Names are announced in the release notes and never renamed silently.

| Metric | Type | Labels | Role |
| --- | --- | --- | --- |
| `info` | gauge 1 | `version`, `core_version`, `mxl_revision`, `role` | both |
| `generator_active` | gauge 0/1 | | gen |
| `generator_audio_muted` | gauge 0/1 | | gen |
| `generator_events_total` | counter | | gen |
| `output_frames_total`, `output_frames_late_total`, `output_frames_missed_total` | counter | `essence` | both; `missed` counts generator video grains that were never written (audio stays 0) |
| `input_state` | gauge 1 for the state | `essence`, `state` | ana |
| `input_resyncs_total` | counter | `essence` | ana |
| `events_total` | counter | `result` (`paired`, `missing_audio`, `missing_video`, `duplicate`, `ambiguous`, `out_of_range`) | ana |
| `grade` | gauge 1 for the grade | `grade` (`pass`, `warn`, `fail`, `invalid`) | ana |
| `av_offset_ms` | gauge (median of the grade window) | | ana |
| `av_offset_deviation_ms` | gauge | | ana |
| `av_offset_seconds` | histogram (signed buckets ±5, 15, 40, 60, 100, 200, 500 ms) | | ana |
| `video_latency_ms`, `audio_latency_ms` | gauge (median), absent without a reference | | ana |
| `video_index_shift_ms`, `audio_index_shift_ms` | gauge | | ana |
| `video_drops_total`, `video_duplicates_total` | counter | | ana |
| `audio_correlation` | gauge | | ana |
| `audio_level_dbfs` | gauge | | ana |
| `reference_sync` | gauge 1 | `type`, `state` | ana |
| `nmos_registry_up` | gauge 0/1 | | both |
| `nmos_activations_total` | counter | | both |

`deploy/grafana/mxl-avsync.json` graphs offset, grade, latencies, drops and events. On the platform the dashboard is generated (`dashboards/build.py`) and every family above must appear on it or say why not (the platform's monitoring contract).

### 16.6 Alerts (proposed for the platform's rules file)

| Alert | Expression (sketch) | For | Severity |
| --- | --- | --- | --- |
| `AVSyncFail` | `mxl_avsync_grade{grade="fail"} == 1` | 30 s | critical |
| `AVSyncWarn` | `mxl_avsync_grade{grade="warn"} == 1` | 2 m | warning |
| `AVSyncNoEvents` | analyzer routed and `rate(mxl_avsync_events_total{result="paired"}[1m]) == 0` | 1 m | warning |
| `AVSyncGeneratorMuted` | `mxl_avsync_generator_audio_muted == 1` | 10 m | info |

The platform owns the rules (`k8s/monitoring/20-rules.yaml.j2`); this repo delivers the proposal with 1.0.

---

## 17. What the platform needs

### 17.1 Ports

The platform's next free pair (2026-10-23): web **8180**, NMOS **3332** (+3333). Both roles use the same defaults; each instance has its own pod IP on the pod network. No MediaMTX ports.

### 17.2 Catalog entries

Two types in `media_function_catalog`, one image:

```yaml
  # mxl-avsync (D-TODO_): AV-sync and latency test signal generator.
  avsync-generator:
    app: mxl-avsync
    short: avgen
    layers: [production]
    network: pod
    web_port: 8180
    websocket: true
    ports: {web: 8180, nmos: 3332, nmos_ws: 3333}
    gpu: none
    config_dir: /config
    env: {AVSYNC_ROLE: generator}
    export: [{path: /api/v1/config/export, file: config.json}]
    state: {volume: config, items: [config.json]}
    widgets: [generator]
    default_values: {format: 1080p50, channels: 2, offsetMs: 0}
    default_resources:
      requests: {cpu: 500m, memory: 512Mi}    # TODO_ from M5
      limits: {cpu: "1", memory: 1Gi}
    values_schema:
      type: object
      additionalProperties: false
      properties:
        format: {enum: [1080p50, 1080p25, 1080i25, 2160p50, 720p50], default: 1080p50, title: Format}
        channels: {type: integer, minimum: 1, maximum: 16, default: 2, title: Audio channels}
        offsetMs: {type: integer, minimum: -500, maximum: 500, default: 0, title: Set AV offset (ms, + = audio late)}
    nmos_ports:
      senders:
        - {label: "AVSync Test Video", essence: video, format: format}
        - {label: "AVSync Test Audio", essence: audio, channels: channels}
    default_flows:
      - {format: 1080p50-v210, count: 1, history_duration_ms: 200}
      - {format: audio-2ch-48k-float32, count: 1, history_duration_ms: 200}
  # mxl-avsync analyzer: one measuring point (video + audio) per instance.
  avsync-analyzer:
    app: mxl-avsync
    short: avana
    layers: [production]
    network: pod
    web_port: 8180
    websocket: true
    ports: {web: 8180, nmos: 3332, nmos_ws: 3333}
    gpu: none
    config_dir: /config
    env: {AVSYNC_ROLE: analyzer}
    export: [{path: /api/v1/config/export, file: config.json}]
    state: {volume: config, items: [config.json, routes.json]}
    widgets: [avsync]
    default_values: {result: true}
    default_resources:
      requests: {cpu: 500m, memory: 512Mi}    # TODO_ from M5
      limits: {cpu: "1", memory: 1Gi}
    values_schema:
      type: object
      additionalProperties: false
      properties:
        result: {type: boolean, default: true, title: Result video flow for the multiviewer}
    nmos_ports:
      receivers:
        - {label: "AVSync In Video", essence: video}
        - {label: "AVSync In Audio", essence: audio}
      senders:
        - {label: "AVSync Result Video", essence: video, when: {result: [true]}}
    default_flows:
      - {format: 540p25-v210, count: 1, history_duration_ms: 200, when: {result: [true]}}   # TODO_ flow size token
```

`env:` per type and the `540p25-v210` flow-size token may not exist in the catalog format yet (`TODO_` the platform decides how a type sets `AVSYNC_ROLE` and sizes a 960×540 flow).

### 17.3 Chart type

A `mxl-common.type.avsync` template (both catalog types), in the style of the scopes type: `AVSYNC_ROLE` and the values as env; `AVSYNC_STATE_DIR=/config`; the MXL env with `MXL_CLEANUP_ON_EXIT=true`; `WEB_PORT`, `NMOS_PORT`; the common NMOS env; `WIDGET_FRAME_ANCESTORS='self' https://designer.<fqdn>`; Service ports `http` and `nmos`; probes; MXL and config mounts; `terminationGracePeriodSeconds` 20.

### 17.4 Monitoring

The dashboard rows and the alert rules of §16.5 and §16.6; `PTP` state for `AVSYNC_PTP_STATUS_URL` if the platform can expose it (open question 6).

### 17.5 Productions

Generators belong in test productions (§11). A typical test production: `avsync-generator` → (path under test) → `avsync-analyzer` per measuring point → multiviewer tiles of the result flows.

---

## 18. Decisions (the maintainer, 2026-10-23)

| # | Question | Decision |
| --- | --- | --- |
| 1 | Name | `mxl-avsync` (repo, image `ghcr.io/leeo86/mxl-avsync`, metrics `mxl_avsync_`) |
| 2 | Language | C++20 like the siblings; dependency-free core reused by the phone app |
| 3 | Real paths | MXL flows and files in this repo. The ST 2110 hour run goes through mxl-st2110-gateway. SRT and SIP/G.711 live inputs arrive as MXL flows from the sibling gateways. File-mode H.264 and G.711 are test aids only. Stage 2 uses those gateways and tests the matrix; this repo adds no transport code |
| 4 | Tolerance | per-stage accuracy −5…+15 ms passes (green); EBU R37 −40…+60 ms warns (orange); outside fails (red) |
| 5 | Deployment | one image, two catalog types (`avsync-generator`, `avsync-analyzer`), `AVSYNC_ROLE` per instance |
| 6 | Analyzer outputs | the `avsync` widget, the result video flow for the multiviewer, Grafana alerts — besides metrics, REST and the UI |
| 7 | Phone | Expo app for iOS and Android, built last |
| 8 | Licence | MIT |

Decided by this spec (from the draft's open list, §17 there), open to review:

- Container default MOV + ProRes 422 HQ + PCM 24-bit (§9.1), confirmed by M1 tests.
- Marker coding: toggle block + Gray-coded strip; QR for identity only (§5).
- Chirp set of four shapes, 30 ms, 700–2500 Hz, 2 events/s (§6.1), confirmed by M0 tests.
- LIVE reference: MXL TAI from the node's PTP-disciplined clock; offsets on index times, latencies on arrival times (§3.3).
- Language of the spec and the code: English, like the siblings. The German draft is kept in `docs/`.

### 18.1 Open questions

1. **Generator placement.** Should the platform allow `avsync-generator` only in test productions, or anywhere with the tags? *Recommendation:* anywhere, with the test-signal tag shown in the designer; the operator decides.
2. **Event rate.** 2/s gives ±1 s pairing range and 120 events per minute; 1/s is calmer to listen to. *Recommendation:* 2/s, settable.
3. **Chirp level.** −12 dBFS peak is clearly audible in a control room. *Recommendation:* −12, with mute in the widget; lower it if operators complain.
4. **Result raster.** 960×540 at 25 Hz is 0.7 MB per grain. *Recommendation:* yes; 1920×1080 only on request.
5. **Event log retention.** 7 days or 100 MB on `/config`. *Recommendation:* as written; the platform's state save does not archive it (only `config.json`, `routes.json`).
6. **PTP state.** Clock selection is URL override, optional ptp4l socket, then kernel fallback (§3.3). The default kernel check cannot distinguish PTP from NTP; every status names `sync_source`, accuracy and reason. A missing NTP TAI offset is explicitly unlocked.
7. **Calibration rig** for the phone: who builds it and from what parts. Decided at M-P1.
8. **Tolerances per use** beyond the default windows (draft question 9): per-production overrides through the analyzer's values. *Recommendation:* add `passMinMs` … to the catalog values once a production needs it.

---

## 19. References

- MXL — https://github.com/dmf-mxl/mxl (pin `218ddaa`); the shared MXL interlaced convention (mxl-multiviewer `docs/mxl-interlaced.md`)
- AMWA IS-04 v1.3, IS-05 v1.2, BCP-007-03, BCP-004-01, BCP-002-01
- SMPTE ST 2059-2 (PTP profile), SMPTE ST 2110-10/-20/-30
- EBU R37 (the relative timing of the sound and vision components of a television signal)
- ITU-R BT.1359-1 (relative timing of sound and vision for broadcasting)
- ATSC IS-191 (relative timing of sound and vision for broadcast operations)
- ITU-T G.711 (PCMU, PCMA)
- ITU-R BT.709-6
- Sibling specifications: `LeeO86/mxl-scopes`, `mxl-test-player`, `mxl-multiviewer`, `mxl-st2110-gateway`, `mxl-srt-gateway`, `mxl-fabrics-agent`, `mxl-audio-mixer` (Appendices A and B: the platform contract)

### 1.0.1 form editing contract

Signal and Settings fields are local drafts. Status/JPEG refreshes do not replace or reconstruct the settings form. Focused or dirty fields retain their contents; Apply/Discard and an Unsaved changes indicator make adoption explicit. Failed writes retain the draft. A successful own write adopts its server response unless the user typed a newer edit while it was pending. Discard reloads current server values.


## Integrated audio timecode v3 (2026-10-27)

The onset-reference, compact protected audio format and white-pulse marker
supersede the legacy-only event-ID/reference descriptions above when
AVSYNC_AUDIO_TIMECODE is 1200 or 600. Generator/analyzers/native iPhone share
one codec. Coded pairs use source plus full common timestamp, with an online
10-second retention window; legacy ±2-period pairing remains only for legacy.
Phone audio-only latency is explicitly estimated from device UTC, with unknown
clock/capture accuracy; this user-authorized extension does not claim PTP lock.
Detailed contract and hardware boundaries: [audio-timecode.md](docs/audio-timecode.md).


### Mobile generator appearance (maintainer addition, 2026-10-28)

The iOS source shares the server pattern renderer: EG1 color bars, fiducials,
QR, timing marker/strip, source-ident area and early/zero/late sync scale. The
native display layer advances the square on the common host-clock timeline.
Name, subtitle and an optional selected image are local persisted settings.
Photo selection exposes only the chosen image; a normalized private copy is
stored independently of Photos. Presentation labels do not change audio/QR
identity or imply source-output calibration. Text rasterization is CoreText
on iOS with the same bundled DejaVu font, avoiding media/GUI dependencies in
avsync-core and desktop rendering dependencies in the mobile binary.

### QR + flash signal (AVS2, 2026-10-29)

The compact audio-timecode generator now uses the shared AVS2 video layout in
file, MXL and iPhone sources. A large version-4 QR (33 modules, correction Q,
four quiet modules per side) sits beside an equal 41-module flash panel with a
four-module gap. All measurement identity is carried in the QR; the Gray strip
and outside fiducials are absent. Colour bars, source labels/image and the
visual sync scale remain. Legacy audio mode retains its original video layout.

AVS2 is `AVS2:` followed by 54 uppercase hexadecimal characters, encoded as QR
alphanumeric data. The 27 big-endian bytes are: flags u8 (bit 0 LIVE, bit 1
estimated phone source; other bits forbidden), source id u32, full event
sequence u64, video send time i64 non-negative nanoseconds (TAI for LIVE,
media timeline for FILE), signed source AV offset i32 in 1/16 ms, format u8
and event rate u8 (1 or 2). Format indices 0–8 are respectively 1080p50,
1080p25, 1080p59.94, 1080i25, 2160p50, 720p60, 720p50,
960x540p25 and its 540p25 alias. Presentation labels are outside the payload.
The independent CRC/FEC audio packet and its onset reference are unchanged.

The QR changes on the source flash's first frame and remains for the complete
event period. No next-event prediction or nearest-chirp association is used.
A decoder passes identified QR corners in normalized capture coordinates to
the dependency-free QrFlashTracker. It projects only sparse photometry samples
in the adjacent panel and static finder patterns; it never rectifies a raster.
Two black observations then two white observations confirm the rising edge,
retaining the first white frame and its original index/arrival metadata.
Mixed transition frames may bridge a bounded 60-ms black-to-white interval.
A QR captured 25 ms to 85% of a source period after that edge identifies it;
a late decoding callback does not change the edge timestamp. Missing identity
or edges, stale geometry, source changes and duplicates cannot create events.
Geometry expires after 750 ms. The normal QR retry target is 100 ms; scans are
bounded to one in flight on iOS. Motion/finder failure withholds measurements.

A crop is supported only while the QR including quiet zone and the sampled
flash area remain visible. Frozen pictures cannot produce repeating events.
AVS2 does not claim the old strip's per-frame drop/duplicate diagnostics.
Optical paths retain unknown capture uncertainty; phone-clock latency remains
explicitly estimated. Digital-path grain timing is still the received frame
index; arrival time remains the first-edge arrival, not QR processing time.

For AVS2 identity, up to 32 sparse 33×33 module-center samples are retained
from the start of each pulse. The later decoded QR is re-encoded using the
same fixed version/ECC/mask-selection rules; at least 80% confidently sampled
modules and zero contradictory confident bits are required in one sample.
Unknown/transition modules are erased, not corrected by a permissive timing
window. A QR already observed before the pulse cannot identify a new pulse.
This prevents frozen QR or skipped-event callbacks from relabelling an edge.
Nayuki is vendored into the portable core for this check, with no new external
media/GUI dependency. Sampling occurs only within the first 100 ms of the pulse and stops after identification. This allows QR pixels to settle after the flash has already started.


### AVS2 presentation (2.0.0)

The app icon is centered with AVsync immediately below it. The configured
source caption is labelled Title in the web and mobile generator controls;
Subtitle is rendered at two-thirds the title scale (minimum one raster scale).
Existing source-name persistence is retained. The obsolete separate header
Title control is removed; legacy API keys remain accepted. The moving sync
box has a vertical black centerline. White scale ticks are foreground and the
box travels underneath them in both native and server renderers. None of
these decorative regions changes QR/flash geometry or audio timing.
