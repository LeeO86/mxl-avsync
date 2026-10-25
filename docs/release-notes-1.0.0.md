# mxl-avsync 1.0.0

The first release. One image is a generator or an analyzer of lip-sync on MXL: the generator writes a `video/v210` test picture and `audio/float32` chirps, and the analyzer reads them and grades the offset. ST 2110, SRT, SIP and fabrics stay in the sibling gateways. The changes are listed in CHANGELOG.md under "1.0.0".

The tag is not cut, and no image digest is published.

## Image

```
ghcr.io/leeo86/mxl-avsync:1.0.0   (also 1.0, 1, when the tag is cut)
```

- Git tag `v1.0.0` is not created.
- MXL `218ddaa0a08c12ffe75fc475ae65aa3d9eef16d7`, also in the label `io.dmf.mxl.revision`.
- nmos-cpp `fe303849527394b03bdedc8f161f377fe458bb62`.
- Runs as uid 1000. The image `HEALTHCHECK` calls `/livez`.
- Latency gauges appear only with a locked PTP reference.
- The preview contract is JPEG polling of `/api/v1/preview/generated.jpg`. On the analyzer that file is the result picture.

## What is in it

The generator and analyzer, the result picture, content-versus-grain times, and latency fields only when the PTP clock is locked. JPEG polling, `no_signal` after a still head, and a generator that fails `/livez` on a leftover domain it does not own. An IS-05 `master_enable: false` stops the analyzer. With `NMOS_REGISTRY_ADDRESS` set, `/readyz` stays down until the node is in the Query API. A live offset change applies on a running generator. The grade becomes invalid after `AVSYNC_STALE_S`.

## For the platform

This is a new application, so nothing on a Kubernetes production platform changes in place. `deploy/k8s/deployment.yaml` is one Deployment per role. It was not applied: the machine that added it has no cluster.

### Network and ports

- Pod network.
- Generator `WEB_PORT` 8180 and `NMOS_PORT` 3332. Analyzer `WEB_PORT` 8181 and `NMOS_PORT` 3334.
- `NMOS_HOST_ADDRESS` is the pod IP. `NMOS_REGISTRY_ADDRESS` points at the platform registry. DNS-SD stays off.
- A port that cannot be bound exits 75. Invalid configuration exits 78. SIGTERM exits 143.
- `terminationGracePeriodSeconds` is 20, above `SHUTDOWN_TIMEOUT_S` (10).

### Resources

From one 1080p50 retest on 2026-10-25: the generator was about 0.6 core and 133 MB RSS (93 MiB cgroup), the analyzer about 0.56 core and 140 MB RSS (54 MiB cgroup). The Deployments request 700m / 256Mi and limit 1500m / 384Mi. There is no second measurement.

Each pod mounts a memory `emptyDir` at `/mxl`. Two pods do not share that volume. A route from one role to the other needs the same MXL volume mounted on both.

### Metrics

Prefix `mxl_avsync_`. Every family is present from process start. `video_latency_ms` and `audio_latency_ms` are not exported; they are on the measurement when the clock is locked. `nmos_registry_up` follows the Query API when a registry is configured.

## Known limits

- PTP-locked latency and a registry on a real network are lab only (`docs/test-matrix.md`).
- The AMWA job (`tests/nmos/amwa.sh`, IS-04-01, IS-04-02, IS-05-01, IS-05-02) is in CI and was not executed on the machine that added it (no Docker). Pass, Not Applicable, Test Disabled, Could Not Test, Manual and Not Implemented are accepted. Any other non-pass must be listed in the expected file.
- The image has no `HEALTHCHECK`. Use the liveness probe.

## Please report back

- The digest you pin.
- Whether `/readyz` stays down until the node is in the Query API.
- Late or missed output grains.
- RSS against the 256Mi request on a Kubernetes production platform.
