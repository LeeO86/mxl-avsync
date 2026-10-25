# mxl-avsync

AV sync and latency measurement for MXL. A generator writes a visible and audible test signal (timing marker, Gray-coded frame strip, QR code, chirps), live into MXL flows or as test files. An analyzer finds the markers and chirps at any point of a chain and reports the AV offset with a pass/warn/fail grade, and — with the platform's PTP/TAI clock — the audio and video latency. A phone app for measuring at a real monitor comes last.

**Status: measuring core, file tools, control plane, live MXL reader and writer, and the phone core.** The container image is `ghcr.io/leeo86/mxl-avsync`, user 1000:1000. `/metrics` exports every family from the first scrape, counters at 0. Latency gauges are not exported. `mxl-avsync` links libmxl when the package is found; `avsync-core` does not. `/readyz` is 200 when the video and audio flows are open. A head that stands still is `no_signal` and `/readyz` is not ready. The analyzer also writes a result picture flow, and the web UI polls `/api/v1/preview/input.jpg` and `/api/v1/preview/generated.jpg` once a second. `AVSYNC_PTP_STATUS_URL` is an `http://` JSON document (`sync_state`, `accuracy_ns`). Latency and the index shifts are reported only when that clock is locked and the QR names the event. `video_content_vs_grain_ms` and `audio_content_vs_grain_ms` compare that content time with the MXL grain timestamp even when the clock is unknown. This repo does not implement ST 2110, SRT, or SIP; those flows come from the sibling gateways. File-mode H.264 and G.711 are test aids. A real phone still needs a device. Read [SPECIFICATION.md](SPECIFICATION.md), [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) and [docs/test-matrix.md](docs/test-matrix.md).

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
- NMOS IS-04/IS-05 with nmos-cpp when the build has it. DNS-SD is off unless `NMOS_DNS_SD=true`. Transport is `urn:x-nmos:transport:mxl`. `/livez`, `/readyz`, `/metrics`. The image `HEALTHCHECK` calls `/livez` on `WEB_PORT` (default 8180).
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
