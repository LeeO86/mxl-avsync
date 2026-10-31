# mxl-avsync

AV sync and latency measurement for MXL and iPhone. Version 2.0 uses a large
QR code beside a flash field and independently decodable audio timecode. File,
live MXL and native iOS sources/receivers share the signal and measuring core.
The analyzer reports audio/video offset and, with a suitable clock reference,
latency. Phone-clock latency is explicitly estimated with unknown physical
accuracy. Android capture is planned.

Get `ghcr.io/leeo86/mxl-avsync:2.0.4` or see the [AVsync project site](https://leeo86.github.io/mxl-avsync/)
for iOS TestFlight information. Upgrade compact-signal generator and receivers
together: old receivers do not understand AVS2. Legacy signals remain supported.
[Signal and validation details](docs/qr-flash.md) · [Release notes](docs/release-notes-2.0.4.md).


**Status: measuring core, file tools, control plane, live MXL reader and writer, and the phone core.** The container image is `ghcr.io/leeo86/mxl-avsync`, user 1000:1000. `/metrics` exports every family from the first scrape, counters at 0. Latency gauges are not exported. `mxl-avsync` links libmxl when the package is found; `avsync-core` does not. `/readyz` is 200 when the process and HTTP service are up and, with NMOS registration configured, the node is listed in the Query API. The generator also requires its output flows to be created. Analyzer routes and signal are independent: `not_routed`, `waiting` and `no_signal` remain ready and are reported in `/statusz`, the input-state metric and the UI. The analyzer also writes a result picture flow, and the web UI polls `/api/v1/preview/input.jpg` and `/api/v1/preview/generated.jpg` once a second. Clock state comes from an optional ptp4l management socket (`AVSYNC_PTP_SOCKET`), otherwise the kernel (`adjtimex(2)`); `AVSYNC_PTP_STATUS_URL` is an explicit override. `/statusz`, `/api/v1/status`, the UI and metrics identify `sync_source` (`ptp4l`, `kernel` or `url`). See [clock reference and deployment requirements](docs/clock-reference.md) for socket permissions, PTP error reporting and NTP TAI setup. Latency and the index shifts are reported only when that clock is locked and the QR names the event. Latency is arrival vs the content timestamp; directly behind the generator it is the generator's own write timing (both essences committed about 20–25 ms early, so negative). The main value is the AV sync, which is what MXL consumers see. For an absolute latency (emission → arrival, never negative, with ± and the clock named), enter the source's emit offsets on the analyzer (`AVSYNC_SOURCE_EMIT_VIDEO_MS` / `AVSYNC_SOURCE_EMIT_AUDIO_MS`, copied from the generator's `/api/v1/status` `emit_offsets`); without them the latency is relative to the source timestamp. Alert rules: `deploy/prometheus/avsync-rules.yaml`; per-release bench over several paths: `tools/bench_paths.py`. `video_content_vs_grain_ms` and `audio_content_vs_grain_ms` compare that content time with the MXL grain timestamp even when the clock is unknown. This repo does not implement ST 2110, SRT, or SIP; those flows come from the sibling gateways. File-mode H.264 and G.711 are test aids. A real phone still needs a device. Read [SPECIFICATION.md](SPECIFICATION.md), [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) and [docs/test-matrix.md](docs/test-matrix.md).

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/avsync-core-tests
./tests/integration/api.sh
./tests/integration/file.sh
./tests/integration/stage2.sh
node --test mobile-app/src/display.test.mjs
```

Needs CMake, Ninja, a C++20 compiler and the FFmpeg development libraries (libavformat, libavcodec, libavutil, libswscale, libswresample).

```bash
./build/mxl-avsync generate --format 1080p50 --duration 60 --offset 0 --out test.mov
./build/mxl-avsync analyze test.mov --manifest test.mov.manifest.json
AVSYNC_ROLE=generator AVSYNC_STATE_DIR=/tmp/avsync ./build/mxl-avsync serve
```

## At a glance

- C++20; one dependency-free core (`avsync-core`) shared by the CLI, the media function and the later phone app.
- `av_offset_ms = audio − video`: **positive = audio late**.
- Grade: pass −5…+15 ms (green), warn −40…+60 ms (orange, EBU R37), fail outside (red).
- CLI: `mxl-avsync generate` (MOV + ProRes + PCM by default, with a JSON manifest), `analyze`, `validate`, `merge`.
- Media function: one image, `AVSYNC_ROLE=generator` or `analyzer`; one measuring point (video + audio) per analyzer.
- Analyzer outputs: REST, WebSocket, `/metrics`, the `avsync` widget, a result video flow for the multiviewer, Grafana alert rules.
- NMOS IS-04/IS-05 with nmos-cpp when the build has it. Node label equals `NMOS_LABEL` verbatim (or `HOST_ID` when unset); the device label appends the role. Sender/receiver labels stay unchanged. DNS-SD is off unless `NMOS_DNS_SD=true`. Transport is `urn:x-nmos:transport:mxl`. An IS-05 receiver PATCH needs `transport_params` (`mxl_domain_id` and `mxl_flow_id`) from the sender's `/active`. A PATCH that carries only `sender_id` increments the activation counter and leaves the analyzer `not_routed`. `/livez`, `/readyz`, `/metrics`. The image `HEALTHCHECK` calls `/livez` on `WEB_PORT` (default 8180).
- Paths in this repo: file round trips, file-mode H.264 and G.711 as test aids, and MXL flows. The ST 2110 hour run goes through mxl-st2110-gateway. SRT and SIP/G.711 live inputs arrive as MXL flows from the sibling gateways. Stage 2 uses those gateways and tests the matrix; there is no transport code here. Phone app (Expo, iOS and Android) in 2.0.

## Ports

| Port | Setting | Default |
| --- | --- | --- |
| Web UI, REST, probes, metrics, widgets | `WEB_PORT` | 8180 |
| NMOS Node and Connection APIs | `NMOS_PORT` | 3332 (+1 reserved) |

## Exit codes

0 `--help`/`--version` (and a CLI run that completed), 75 a port or another startup failure, 78 invalid configuration, 143 SIGTERM/SIGINT. CLI `analyze`: 2 when no event was found.

## Licence

MIT (`LICENSE`).

## Ready-to-play test clips

See [testfiles/README.md](testfiles/README.md) for eight verified AV-sync clips in MP4, MOV and MKV (720p to 4K), known offsets, manifests, analysis reports and regeneration commands.

Signal and Settings forms keep local drafts across status/JPEG polling, with an Unsaved changes indicator and Apply/Discard. Only successful Apply or explicit Discard adopts the server settings; failed saves and newer edits made during a save are retained.


## Audio timecode and white flash

The default signal carries independently decodable audio timecode at 1200 baud.
At zero offset, each white flash starts with the chirp. Choose 1 or 2 chirps/s
in Signal; 600 baud and legacy compatibility are also available. See
[the format, phone feedback, audio-only latency and tests](docs/audio-timecode.md).

## Test-picture labels

The Signal form's **Title** is the large caption below the sync scale;
**Subtitle** is smaller. For existing deployments the saved `source` field and
`AVSYNC_SOURCE` keep their names and values. The obsolete header `title` /
`AVSYNC_TITLE` remains accepted by the legacy API but is not a second title
control for AVS2. Both container and iPhone show the centered AVsync brand.
