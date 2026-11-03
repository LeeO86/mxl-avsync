# mxl-avsync 2.1.0

A new video marker, AVS3, that a camera can time to about 1–2 ms on any
display. Ports, environment, NMOS resources, metrics and dependency pins are
unchanged from 2.0.7.

## AVS3 optical code

- **Why.** AVS2 timed one white flash at 70 % of its brightness. On a real
  panel the brightness ramp depends on the panel (response time, local dimming,
  frame-rate conversion) and on the camera (auto exposure, tone curve), so the
  phone reading moved by up to about 25 ms between panels.
- **What changed.** The flash panel is split into two fields, A (left) and B
  (right), that are always opposite: rest state A black, B white. Around each
  event they swap 8 times in a fixed pattern of 40 ms chips (80 ms at
  1 event/s). The total light stays constant, so auto exposure and local
  dimming do not react. The analyzer times every moment where A = B and
  averages them. The pattern also carries the event parity.
- The QR now names each event from half a period before its burst, with the
  payload prefix `AVS3:` and the same fields as AVS2.
- Layout, QR position, audio timecode and the `legacy` mode are unchanged.
- Simulation (`avsync-optical-sim`, p5–p95 of the 4-event mean): 0.1–1.1 ms
  for broadcast, LCD, mini-LED, WebRTC and 30–60 fps cameras; 2.4 ms for a bad
  WebRTC path. AVS2 in the same models: 0.9–22 ms. Details in
  [qr-flash.md](qr-flash.md).

## What changes for the platform

- **Update every mxl-avsync instance together.** A 2.1.0 analyzer creates no
  video events from an AVS2 source (2.0.x generator or a file made with 2.0.x),
  and a 2.0.x analyzer does not read AVS3. Files for playback must be
  generated again with 2.1.0.
- The phone app TestFlight build 11 (Mobile 2.0.8) reads only AVS2. The
  matching phone app is Mobile 2.1.0 (not released yet).
- Events gain the flag `optical_code`. No metric is new, renamed or removed.
- Generator render time per grain (one core, live writer): 1080p50 p50
  0.89 ms, p99 3.8 ms; 2160p50 p50 3.3 ms. Same as 2.0.7.
- In-place upgrade; no settings change.
