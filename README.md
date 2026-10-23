# mxl-avsync

AV sync and latency measurement for MXL. A generator writes a visible and audible test signal (timing marker, Gray-coded frame strip, QR code, chirps), live into MXL flows or as test files. An analyzer finds the markers and chirps at any point of a chain and reports the AV offset with a pass/warn/fail grade, and — with the platform's PTP/TAI clock — the audio and video latency. A phone app for measuring at a real monitor comes last.

**Status: design (decisions taken 2026-10-23).** There is no code yet. Read [SPECIFICATION.md](SPECIFICATION.md) and [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md). The original German draft is in [docs/draft-2026-10-23-de.md](docs/draft-2026-10-23-de.md).

## At a glance

- C++20; one dependency-free core (`avsync-core`) shared by the CLI, the media function and the later phone app.
- `av_offset_ms = audio − video`: **positive = audio late**.
- Grade: pass −5…+15 ms (green), warn −40…+60 ms (orange, EBU R37), fail outside (red).
- CLI: `mxl-avsync generate` (MOV + ProRes + PCM by default, with a JSON manifest), `analyze`, `validate`, `merge`.
- Media function: one image, `AVSYNC_ROLE=generator` or `analyzer`; one measuring point (video + audio) per analyzer.
- Analyzer outputs: REST, WebSocket, `/metrics`, the `avsync` widget, a result video flow for the multiviewer, Grafana alert rules.
- NMOS IS-04/IS-05 with BCP-007-03; `/livez`, `/readyz`, `/metrics`.
- Paths in 1.0: file round trips (incl. transcodes), MXL, the ST 2110 loop. SRT/H.264, cross-node and G.711 live in 1.1. Phone app (Expo, iOS and Android) in 2.0.

## Ports

| Port | Setting | Default |
| --- | --- | --- |
| Web UI, REST, probes, metrics, widgets | `WEB_PORT` | 8180 |
| NMOS Node and Connection APIs | `NMOS_PORT` | 3332 (+1 reserved) |

## Exit codes

0 `--help`/`--version` (and a CLI run that completed), 75 a port or another startup failure, 78 invalid configuration, 143 SIGTERM/SIGINT. CLI `analyze`: 2 when no event was found.

## Licence

MIT (`LICENSE`).
