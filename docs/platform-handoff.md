# Platform hand-off

For the platform repository, when a build with libmxl is available:

- Image `ghcr.io/leeo86/mxl-avsync`, one image, two catalog types `avsync-generator` (`AVSYNC_ROLE=generator`) and `avsync-analyzer` (`AVSYNC_ROLE=analyzer`).
- Ports: web 8180, NMOS 3332, NMOS websocket reservation 3333. No MediaMTX.
- Widgets: `generator`, `avsync`.
- State: `/config` with `config.json` and, for the analyzer, `routes.json`.
- Metrics prefix `mxl_avsync_`. Dashboard `deploy/grafana/mxl-avsync.json`. Alert proposal `deploy/prometheus/avsync-rules.yaml`.
- The process cannot see PTP lock. Set `AVSYNC_PTP_STATUS_URL` to an `http://` document `{"sync_state":"locked"|"unlocked"|"unknown","accuracy_ns":"..."}`. Until that read succeeds, `sync_state` is `unknown` and latency is not reported. A locked reading plus the QR `T_k` fills arrival latency on live events.
- Demo: `docker/docker-compose.demo.yaml` and `deploy/k8s/avsync.yaml`. NMOS and DNS-SD stay off. There is no registry.
- Generators should be placed in test productions. This repo does not enforce that.

Catalog CPU numbers are still the spec placeholders. See `docs/performance.md`.
