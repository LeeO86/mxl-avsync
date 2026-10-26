# Clock reference and deployment requirements

AV offset can be measured without a shared clock. Absolute arrival latency needs a valid, disciplined TAI reference; software clock status does not independently validate physical accuracy.

## Source selection

1. `AVSYNC_PTP_STATUS_URL`, when set, is an explicit override: `sync_source=url`. It accepts `http://` JSON with `sync_state` and `accuracy_ns`. Failure reports unknown; it does not silently choose another reference.
2. Otherwise, `AVSYNC_PTP_SOCKET` optionally selects a ptp4l management socket, for example `/var/run/ptp4l`: `sync_source=ptp4l`. Only GET management requests are sent, for `TIME_STATUS_NP` and `PORT_DATA_SET`. A valid but unlocked response stays on this source. Socket/access/protocol failure logs once and falls back to the kernel.
3. Without a usable socket, read-only `adjtimex(2)` supplies `sync_source=kernel`. No clock-setting capability is required.

`/statusz`, `/api/v1/status` and the UI show the selected source, state and reason. PTP status includes grandmaster presence/identity, signed master offset in ns and port state. Metrics expose source selection and `mxl_avsync_ptp_master_offset_seconds` / `mxl_avsync_ptp_gm_present`, initialized to zero before a PTP reading. A zero startup metric is not evidence of lock.

## ptp4l management socket

A ptp4l reading is locked only when a grandmaster is present, port state is SLAVE or MASTER, and the absolute master offset is at most `AVSYNC_CLOCK_MAX_ERROR_US` (default 100 µs). Accuracy is the absolute offset in ns. Grandmaster MASTER state is valid on the grandmaster node itself.

The socket is optional and unset by default. The service does not start ptp4l or change its configuration. A read-only hostPath mount avoids filesystem modification; Unix datagram communication still requires the socket's own permissions to allow uid 1000. Use the host's permitted group via `supplementalGroups`; do not make the socket world-writable.

The client uses an abstract Linux Unix-domain reply endpoint. Abstract endpoints are scoped to the network namespace, so this optional direct-socket mode requires **host networking**, in addition to mounting the socket. A socket file mount alone in an isolated pod does not provide a usable reply path; failed queries fall back to the kernel. Host networking is not required for the default kernel mode or the URL override.

Example Kubernetes fragment (replace the group number with the host's actual socket group):

```yaml
spec:
  hostNetwork: true
  dnsPolicy: ClusterFirstWithHostNet
  securityContext:
    runAsUser: 1000
    runAsGroup: 1000
    supplementalGroups: [1234]  # actual host group allowed to use ptp4l
  containers:
    - name: avsync
      image: ghcr.io/leeo86/mxl-avsync:1.0.1
      env:
        - {name: AVSYNC_PTP_SOCKET, value: /var/run/ptp4l}
      volumeMounts:
        - {name: ptp-management, mountPath: /var/run/ptp4l, readOnly: true}
  volumes:
    - name: ptp-management
      hostPath: {path: /var/run/ptp4l, type: Socket}
```

For Docker use the host network, mount the socket read-only, and add the permitted host group. Some host configurations restrict the management socket; on access failure inspect the logged reason and use the kernel or a node-agent URL instead.

## Kernel PTP and NTP

Kernel lock needs `STA_UNSYNC` clear and TAI−UTC greater than zero. If `esterror` **and** `maxerror` are both at least 16000000 µs (the kernel default phase limit), the error is not reported: accuracy is unknown (`accuracy_ns=-1` internally), and the reason says `error not reported (PTP discipline)`. This supports a host disciplined by ptp4l/phc2sys without treating the default 16-second estimate as a measured error. It does not turn unknown accuracy into a numerical guarantee.

When the kernel reports a real estimated error, such as with chrony or ntpd, the configured `AVSYNC_CLOCK_MAX_ERROR_US` limit is enforced. An unsynchronized kernel or missing TAI offset is unlocked with an explicit reason. Kernel state alone cannot identify the disciplining daemon.

For NTP clusters configure the daemon's leap-second source so it sets TAI−UTC: for example chrony `leapsectz right/UTC` (with the timezone leap data installed), or an ntpd leap file. Merely enabling NTP can leave the TAI offset at zero. Verify synchronization, leap status, `adjtimex` TAI and error estimates on the actual host. Typical local-network NTP accuracy is about 0.1–1 ms; it is not PTP-quality accuracy and may exceed the default 100 µs limit. Set an operational limit appropriate to the measured environment and report the actual estimate; raising the limit is not an accuracy calibration.
