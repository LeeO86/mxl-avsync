# mxl-avsync 1.0.1

Deployment fixes for readiness, NMOS labels, clock references and web settings. AV offset remains positive when audio is late. The MXL and nmos-cpp dependency pins are unchanged.

## Image and upgrade

`ghcr.io/leeo86/mxl-avsync:1.0.1` (also `1.0` and `1`) is produced by the matching release-tag workflow. The image continues to run as uid/gid 1000 and uses `/livez` for its health check. No GPU is required. Existing configuration remains compatible; `AVSYNC_PTP_SOCKET` is optional and defaults to unset.

## Readiness

An analyzer can be ready before its receivers are routed. `/readyz` returns 200 when the process and HTTP service are healthy and, with `NMOS_ENABLE=true` and a registry configured, its node is listed in the Query API. The generator additionally waits for creation of its video and audio output flows. Failed process health or a missing registry listing returns 503.

`not_routed`, `waiting` and `no_signal` do not fail analyzer readiness. `/statusz` and `/api/v1/status` expose `media_ready`, `input_state` and `detail`; input-state metrics and the UI show media health separately. This lets a controller wait for ready pods before connecting them with IS-05. Existing readiness-probe endpoints can stay unchanged.

## NMOS labels

The node label is `NMOS_LABEL` exactly, falling back to `HOST_ID` when unset. The role is appended to the device label, for example `Example analyzer`. Sender and receiver labels and resource IDs are unchanged. Controllers matching node labels by equality can use the configured label directly.

## Clock reference

Clock selection is an explicit URL override, otherwise an optional ptp4l management socket, otherwise the kernel. `/statusz`, the API, UI and metrics report the selected `sync_source` (`url`, `ptp4l`, `kernel`) and a reason. PTP status also exposes grandmaster identity/presence, port state and signed master offset; offset/presence metrics exist from startup.

- `AVSYNC_PTP_STATUS_URL` continues to accept `http://` JSON `{"sync_state","accuracy_ns"}` and wins when set.
- `AVSYNC_PTP_SOCKET` optionally issues GET management queries for `TIME_STATUS_NP` and `PORT_DATA_SET`. Lock needs a present grandmaster, SLAVE or MASTER state, and absolute offset within `AVSYNC_CLOCK_MAX_ERROR_US`. Socket/protocol failure logs once and falls back to the kernel; valid unlocked PTP status stays selected.
- The default kernel reader treats `esterror` and `maxerror` both at least 16000000 µs as unreported. A synchronized host with positive TAI offset can be locked with unknown accuracy and reason `error not reported (PTP discipline)`, supporting phc2sys. Real reported NTP error estimates retain the configured limit.
- NTP also needs TAI−UTC configured by the daemon, such as chrony `leapsectz right/UTC` or an ntpd leap file. Missing TAI remains unlocked. Typical local-network NTP accuracy is 0.1–1 ms, without a PTP accuracy claim.

See [clock deployment requirements](clock-reference.md) for the read-only socket mount, uid 1000 socket permissions and supplemental group. The abstract management reply endpoint also requires host networking; a mounted socket alone in an isolated pod does not suffice. Default kernel mode needs no additional mounts or privileges.

## Web forms

Signal and Settings fields now keep local drafts during status and JPEG polling. Unsaved changes, Apply and Discard make adoption explicit. Failed saves retain input, and a successful save cannot erase a newer edit typed while that request was pending. Discard reads current server values. Image/status polling does not reconstruct the settings form.

## Embedded test-picture recognition

Expanded validation reproduced a 1.0.0 bug in `stage2.sh`: on a scaled and padded signal, the locator could select QR/text squares instead of all four picture corners. The fix preserves spatial corner candidates, refines them locally, checks geometry/reference strip/parity, and rectifies to a stable 640×360 raster. The unchanged Stage-2 integration passes; a full-renderer regression checks three sequences, both marker states and all corner coordinates (54 assertions). This is a digital file-path recognition fix, not a claim of calibrated phone-camera accuracy.

## Validation and limits

Regression coverage includes readiness before routing, unroute/reroute, signal loss and Query API disappearance; exact node labels for both roles; kernel classification, management-message encoding/decoding, fake UDS source selection and URL override; form polling, Apply persistence, failed writes and edits during a pending save. A real-browser test uses an isolated service, waits through at least three status polls while typing, and checks Apply/Discard.

Protocol mocks do not establish physical PTP accuracy. ST 2110 hour runs, independent LED/piezo phone calibration and cross-node hardware validation require their respective lab equipment and are not implied by this release.
