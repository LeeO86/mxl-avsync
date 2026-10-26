# mxl-avsync 1.0.1 (unreleased)

## Readiness

An analyzer can be ready before its receivers are routed. `/readyz` returns 200 when the process and HTTP service are healthy and, with `NMOS_ENABLE=true` and a registry configured, its node is listed in the Query API. The generator additionally waits for creation of its video and audio output flows. Failed process health or a missing registry listing returns 503.

`not_routed`, `waiting` and `no_signal` are normal analyzer states and do not fail readiness. `/statusz` and `/api/v1/status` expose `media_ready`, `input_state` and `detail`; input-state metrics and the UI continue to show media health. This lets a controller wait for all pods to be ready before connecting them with IS-05. No probe configuration change is required.

The image health check continues to use `/livez`. No release tag or image version is created by this change.
