# Platform hand-off

For the platform repository, when a build with libmxl is available:

- Image `ghcr.io/leeo86/mxl-avsync`, one image, two catalog types `avsync-generator` (`AVSYNC_ROLE=generator`) and `avsync-analyzer` (`AVSYNC_ROLE=analyzer`).
- Ports: web 8180, NMOS 3332, NMOS websocket reservation 3333. No MediaMTX.
- Widgets: `generator`, `avsync`.
- State: `/config` with `config.json` and, for the analyzer, `routes.json`.
- Metrics prefix `mxl_avsync_`. Dashboard `deploy/grafana/mxl-avsync.json`. Alert proposal `deploy/prometheus/avsync-rules.yaml`.
- The process cannot see PTP lock. Until `AVSYNC_PTP_STATUS_URL` is set, `sync_state` is `unknown` and latencies are not given a numeric uncertainty.
- Generators should be placed in test productions. This repo does not enforce that.

Catalog CPU numbers are still the spec placeholders. See `docs/performance.md`.
