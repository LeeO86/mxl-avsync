# Platform hand-off

For the platform repository, when a build with libmxl is available:

- Image `ghcr.io/leeo86/mxl-avsync`, one image, two catalog types `avsync-generator` (`AVSYNC_ROLE=generator`) and `avsync-analyzer` (`AVSYNC_ROLE=analyzer`).
- Ports: web 8180, NMOS 3332, NMOS websocket reservation 3333. No MediaMTX.
- Widgets: `generator`, `avsync`.
- State: `/config` with `config.json` and, for the analyzer, `routes.json`.
- Metrics prefix `mxl_avsync_`. Dashboard `deploy/grafana/mxl-avsync.json`. Alert proposal `deploy/prometheus/avsync-rules.yaml`.
- Clock check: by default the process reads the kernel clock with `adjtimex(2)` (read-only, no privilege, the host clock inside a container). `locked` means STA_UNSYNC is clear, the TAI offset is set and `esterror` is at most `AVSYNC_CLOCK_MAX_ERROR_US` (default 100); `unlocked` otherwise. The kernel cannot tell PTP from NTP: "locked" is "clock disciplined and TAI set". `AVSYNC_PTP_STATUS_URL` stays as an optional override: an `http://` document `{"sync_state":"locked"|"unlocked"|"unknown","accuracy_ns":"..."}` from a node agent that knows PTP specifically. A locked reading plus the QR `T_k` fills arrival latency on live events.
- Demo: `docker/docker-compose.demo.yaml` and `deploy/k8s/avsync.yaml`. NMOS and DNS-SD stay off. There is no registry.
- Generators should be placed in test productions. This repo does not enforce that.

Catalog CPU numbers are still the spec placeholders. See `docs/performance.md`.
