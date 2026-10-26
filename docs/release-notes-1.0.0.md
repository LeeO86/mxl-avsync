# mxl-avsync 1.0.0

For the revised readiness contract, see [1.0.1 (unreleased)](release-notes-1.0.1.md). The descriptions below record 1.0.0 behaviour.

The first release. One image is a generator or an analyzer of lip-sync on MXL: the generator writes a `video/v210` test picture and `audio/float32` chirps, and the analyzer reads them and grades the offset. ST 2110, SRT, SIP and fabrics stay in the sibling gateways. The changes are listed in CHANGELOG.md under "1.0.0".

Pushing the tag `v1.0.0` builds and pushes the image and creates the GitHub release from this file.

## Image

```
ghcr.io/leeo86/mxl-avsync:1.0.0   (also 1.0 and 1)
```

- The `v*.*.*` tag workflow fails when the tag differs from `kAppVersion` or the CMake version.
- MXL `218ddaa0a08c12ffe75fc475ae65aa3d9eef16d7`, also in the label `io.dmf.mxl.revision`.
- nmos-cpp `fe303849527394b03bdedc8f161f377fe458bb62`.
- Runs as uid 1000. The image `HEALTHCHECK` calls `/livez`.
- Latency fields appear only while `sync_state` is `locked`.
- The preview contract is JPEG polling of `/api/v1/preview/generated.jpg`. On the analyzer that file is the result picture.

## What is in it

The generator and analyzer, a Measurement page that shows every value, a self-contained clock check, the result picture, content-versus-grain times, and latency fields only when the PTP clock is locked. JPEG polling, `no_signal` after a still head, and a generator that fails `/livez` on a leftover domain it does not own. An IS-05 `master_enable: false` stops the analyzer. With `NMOS_REGISTRY_ADDRESS` set, `/readyz` stays down until the node is in the Query API. A live offset change applies on a running generator. The grade becomes invalid after `AVSYNC_STALE_S`.

## For the platform

This is a new application, so nothing on a Kubernetes production platform changes in place. `deploy/k8s/deployment.yaml` is one Deployment per role. It was not applied: the machine that added it has no cluster.

### Image and process

- Image `ghcr.io/leeo86/mxl-avsync:1.0.0`, one image, role by `AVSYNC_ROLE=generator|analyzer`. Runs as uid/gid 1000. No GPU.
- Health: `/livez` (the image `HEALTHCHECK` uses it), `/readyz` (200 when the flows are open and, with a registry, the node is listed), `/statusz`, `/metrics`.
- Exit codes: 75 port cannot be bound, 78 invalid configuration, 143 SIGTERM.

### Environment variables

| Variable | Meaning |
|---|---|
| `AVSYNC_ROLE` | `generator` or `analyzer` |
| `AVSYNC_STATE_DIR` | state directory, default `/config` (`config.json`, analyzer `routes.json`) |
| `WEB_PORT`, `NMOS_PORT` | ports, see below |
| `NMOS_ENABLE`, `NMOS_DNS_SD` | NMOS node on or off; DNS-SD stays off |
| `NMOS_HOST_ADDRESS`, `NMOS_REGISTRY_ADDRESS` | pod IP; the platform registry. `/readyz` waits for the node in the Query API |
| `MXL_DOMAIN_SCAN_PATH`, `MXL_OUTPUT_DOMAIN_DIR` | where flows are read and written (`/mxl`) |
| `MXL_CLEANUP_ON_EXIT` | remove the output domain on exit |
| `AVSYNC_CLOCK_MAX_ERROR_US` | largest kernel `esterror` that still counts as `locked` (default 100) |
| `AVSYNC_PTP_STATUS_URL` | optional `http://` JSON `{"sync_state","accuracy_ns"}` that overrides the kernel clock check |
| `AVSYNC_STALE_S` | grade becomes `invalid` after this long without a valid event |

All settings are also in `GET/PUT /api/v1/config`.

### Network and ports

- Pod network. Generator `WEB_PORT` 8180 and `NMOS_PORT` 3332. Analyzer `WEB_PORT` 8181 and `NMOS_PORT` 3334 in the shipped Deployments (image defaults 8180 / 3332).
- `terminationGracePeriodSeconds` is 20, above `SHUTDOWN_TIMEOUT_S` (10).

### NMOS

- Generator: two senders, "AVSync Test Video" (`video/v210`, default 1080p50) and "AVSync Test Audio" (`audio/float32`, 48 kHz). Transport MXL. Flows, senders and the device carry the tag `urn:x-mxl:tag:test-signal/v1.0`.
- Analyzer: two receivers, "AVSync In Video" and "AVSync In Audio". BCP-004-01 constraint sets: video `video/v210`, YCbCr-4:2:2, 10 bit, BT.709, 1920x1080 at 25, 50 and 59.94 progressive, 1920x1080 25 interlaced (tff), 3840x2160 50 and 1280x720 50 progressive; audio `audio/float32`, 48 kHz, 32 bit, 1 to 64 channels.
- The process never sends IS-05 to another device. IS-05 `master_enable: false` on a receiver stops the analyzer reader.

### Clock reference

Without a URL the process reads the kernel clock with `adjtimex(2)`: read-only, no privilege, and inside a container it is the host clock. `locked` needs STA_UNSYNC clear, the TAI offset set (37 s) and `esterror` within `AVSYNC_CLOCK_MAX_ERROR_US`. It cannot tell PTP from NTP. Latency fields exist only while `locked`.

### Resources

Measured at 1080p50: the generator about 0.55 core and 136 MB, the analyzer about 0.55 core and 160 MB. The Deployments request 700m / 256Mi and limit 1500m / 384Mi. Each pod mounts a memory `emptyDir` at `/mxl`; a route from one role to the other needs the same MXL volume on both. There is no second measurement.

### Metrics

Prefix `mxl_avsync_`. Every family is exported from process start. `video_latency_ms` and `audio_latency_ms` are not metrics; they are on the measurement when the clock is locked.

- `mxl_avsync_info{version,core_version,mxl_revision,role}`
- `mxl_avsync_generator_active`, `mxl_avsync_generator_audio_muted`, `mxl_avsync_generator_events_total`
- `mxl_avsync_output_frames_total{essence}`, `mxl_avsync_output_frames_late_total{essence}`, `mxl_avsync_output_frames_missed_total{essence}`
- `mxl_avsync_input_state{essence,state}` (states `waiting`, `no_signal`, `not_routed`, `running`, `format_mismatch`)
- `mxl_avsync_input_resyncs_total{essence}`
- `mxl_avsync_events_total{result}` (`paired`, `missing_audio`, `missing_video`, `duplicate`, `ambiguous`, `out_of_range`)
- `mxl_avsync_grade{grade}` (`pass`, `warn`, `fail`, `invalid`)
- `mxl_avsync_av_offset_ms`, `mxl_avsync_av_offset_deviation_ms`
- `mxl_avsync_av_offset_seconds` histogram (`_bucket{le}`, `_sum`, `_count`)
- `mxl_avsync_video_drops_total`, `mxl_avsync_video_duplicates_total`
- `mxl_avsync_audio_correlation`, `mxl_avsync_audio_level_dbfs`
- `mxl_avsync_reference_sync{type="tai-ptp",state}` (`locked`, `unlocked`, `unknown`)
- `mxl_avsync_clock_synchronized`, `mxl_avsync_clock_esterror_seconds`, `mxl_avsync_clock_tai_offset_seconds`
- `mxl_avsync_nmos_registry_up`, `mxl_avsync_nmos_activations_total`

## Known limits

- PTP-locked latency and a registry on a real network are lab only (`docs/test-matrix.md`).
- The AMWA job (`tests/nmos/amwa.sh`, IS-04-01, IS-04-02, IS-05-01, IS-05-02) is in CI and was not executed on the machine that added it (no Docker). Pass, Not Applicable, Test Disabled, Could Not Test, Manual and Not Implemented are accepted. Any other non-pass must be listed in the expected file.
- The kernel clock check cannot tell PTP from NTP: `locked` means the clock is disciplined and the TAI offset is set. Set `AVSYNC_PTP_STATUS_URL` for a PTP-specific answer from a node agent.

## Please report back

- The digest you pin.
- Whether `/readyz` stays down until the node is in the Query API.
- Late or missed output grains.
- RSS against the 256Mi request on a Kubernetes production platform.
