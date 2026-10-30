# Platform hand-off

For the platform repository, when a build with libmxl is available:

- Image `ghcr.io/leeo86/mxl-avsync`, one image, two catalog types `avsync-generator` (`AVSYNC_ROLE=generator`) and `avsync-analyzer` (`AVSYNC_ROLE=analyzer`).
- Ports: web 8180, NMOS 3332, NMOS websocket reservation 3333. No MediaMTX.
- Widgets: `generator`, `avsync`.
- State: `/config` with `config.json` and, for the analyzer, `routes.json`.
- Metrics prefix `mxl_avsync_`. Dashboard `deploy/grafana/mxl-avsync.json`. Alert proposal `deploy/prometheus/avsync-rules.yaml`.
- Clock check: by default the process reads the kernel clock with `adjtimex(2)` (read-only, no privilege, the host clock inside a container). `locked` means STA_UNSYNC is clear, the TAI offset is set and `esterror` is at most `AVSYNC_CLOCK_MAX_ERROR_US` (default 100); `unlocked` otherwise. The kernel cannot tell PTP from NTP: "locked" is "clock disciplined and TAI set". `AVSYNC_PTP_STATUS_URL` stays as an optional override: an `http://` document `{"sync_state":"locked"|"unlocked"|"unknown","accuracy_ns":"..."}` from a node agent that knows PTP specifically. A locked reading plus the QR `T_k` fills arrival latency on live events. The URL page may also send `grandmaster`, `port_state`, `ptp4l_offset_ns`, `phc2sys_offset_ns` and `path_delay_ns` (2.0.2); they then show in the status instead of `gm_present false` / `port_state unknown`. See [clock reference](clock-reference.md).
- Latency on a direct generator → analyzer path is the generator's own write timing (both essences about 20–25 ms early, so negative). The AV sync value is what MXL consumers see. For a chain latency, give analyzers a Query API (`NMOS_QUERY_ADDRESS`, or the registry address they already use): they read the generator's flow tag `urn:x-mxl:tag:avsync-emit-offset-ms/v1.0` and subtract it. Behind a gateway the routed flow has no such tag and chain latency stays absent.
- Generator timing: `AVSYNC_VIDEO_LEAD_MS` (default 20, restart) and `AVSYNC_AUDIO_LEAD_MS` (default 20, restart). The defaults need no change.
- Demo: `docker/docker-compose.demo.yaml` and `deploy/k8s/avsync.yaml`. NMOS and DNS-SD stay off. There is no registry.
- Generators should be placed in test productions. This repo does not enforce that.

Catalog CPU numbers are still the spec placeholders. See `docs/performance.md`.
