# QR + optical code (AVS3)

AVS3 (2.1.0) replaces the AVS2 single white flash. The layout is unchanged:
one large QR and a square panel beside it. The panel is now split into two
fields: **A** (left half) and **B** (right half). They are always opposite, one
white and one black, so the panel's total light stays the same.

## The signal

- **Rest:** A black, B white.
- **Burst:** around each event the fields swap 8 times at fixed chip offsets
  (transition 0 is the event time). A chip is the whole number of frames
  closest to 40 ms at 2 events/s (80 ms at 1 event/s, for 30 fps cameras):
  1 frame at 25p, 2 at 50p/59.94p, 2 fields at 1080i25.
  - Even events: chips 0, 1, 2, 4, 5, 6, 7, 8.
  - Odd events: chips 0, 1, 2, 3, 4, 6, 7, 8.
  
  The moved 2-chip run tells even from odd events. Both patterns have 4 rising
  and 4 falling transitions and end in the rest state.
- **QR:** names event k from half a period before its burst to half a period
  after it. Since AVS4 the text is `AVS4:` + base45 (version 4, correction M);
  it also carries the test id, the generator name and the sender calibration
  (see the specification). It is still and readable during the whole burst.
- **Audio:** unchanged 1200/600-baud timecode with CRC/FEC, onset at the event.

## Why it is stable on any display and camera

Every transition is the same physical change: one field gets brighter, the
other darker. The analyzer times the moment **A = B** for each transition:
- after scaling each field to its own black and white,
- after undoing the camera tone curve (about gamma 2.2),
- from the samples on the edge itself. Each sample stands for the centre of its
  exposure. A slow edge gets a straight-line fit, a sharp edge inside one
  exposure the box model, and an edge between two exposures the gap middle.

The burst time is the robust mean of all 8 crossings (at least 6 are needed,
3 of each direction). Effects that are the same for every transition cancel or
only shift all of them together: display response, local dimming, auto exposure,
the camera's tone curve, unequal field brightness, rolling shutter on the
panel's rows. Frame-rate conversion (50p on a 60 Hz screen, WebRTC) moves single
transitions by up to one refresh; the average over 8 transitions and the
8-event reading window smooth that out.

## Identity

A burst gets the sequence of a QR seen within 3/8 of a period of it whose
parity matches the burst pattern. Without such a QR (for example a missed
decode), the analyzer counts on from the last named burst on the event grid,
again only with matching parity. A frozen picture makes no burst, so it cannot
create events; a QR of the wrong parity or another source never names a burst.

## Simulation

`avsync-optical-sim` (tests/tools/optical_sim.cpp) models the source, the
transport (WebRTC jitter and drops), the display (refresh rate, rise and fall
times, local-dimming backlight) and the camera (rate, exposure, auto gain,
clipping, tone curve, noise, dropped frames, free-running clocks). It reports
each event's error against the moment the light really changed. Results
(p5–p95 per event / of the 4-event mean, ms):

| Case | AVS2 70 % flash | AVS3 burst |
| --- | --- | --- |
| Broadcast OLED / LCD 50 Hz | 3.7 / 1.0 | 0.1–0.3 / 0.1 |
| Slow LCD 60 Hz, 50p | 3.7 / 1.0 | 1.8 / 0.3 |
| WebRTC LCD 60 Hz | 3.7 / 0.9 | 2.0 / 1.1 |
| Mini-LED 120 Hz | 4.0 / 1.6 | 1.8 / 0.3 |
| Camera 60 fps | 15.0 / 11.7 | 1.6 / 1.1 |
| Camera 30 fps, 1 event/s | 29.5 / 22.4 | 0.7 / 0.4 |
| Bad WebRTC (15 ms jitter, 3 % drops) | 7.4 / 4.1 | 4.6 / 2.4 |

A short camera exposure (2 ms at 60 fps) gives 8 ms: the apps expose for the
full frame interval. A 30 fps camera finds only about 40 % of the bursts at
2 events/s; use 1 event/s there.

## Tests

- Core: payload round trip and burst schedule; cropped digital frames (exact
  grain times); static pictures; wrong-parity and stuck QR; a simulated
  240 fps camera through the real renderer within 0.5 ms; the phone analyzer
  end to end (+20 ms AV, 30/50 ms latency, spread below 1 ms).
- Integration: file render/encode/analyze, timecode, stage 2.
- Native iOS (`npm run test:ios-native`): the same end-to-end case through the
  real bridge, CoreMedia buffers and Vision.
