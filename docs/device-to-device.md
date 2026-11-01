# Direct device testing and calibration

## Direct sender clock and picture/sound (local implementation, 2026-11-01)

Use **Generate → Direct clock for another device** on an iPhone/iPad, start
the signal and note the six-character code. On the receiver, open **Settings →
Direct device test → Find local senders**, choose that code and allow Local
Network on both devices. Keep both on the same LAN. Film the sender directly
and leave only its sound audible. A sender restart creates a new source ID and
code; pair again. Repeat with the devices swapped, then compare to the Mac.

For the same-monitor comparison without WebRTC, build the native Mac sender:

```sh
python3 tools/direct-device/build.py
open "/tmp/avsync-direct-device/AVsync Direct Test.app"
# Optional: one event per second rather than two
open "/tmp/avsync-direct-device/AVsync Direct Test.app" --args --one-hz
```

macOS 14+, Xcode/command-line tools and a local native iOS build are required.
The helper is built locally, uses the shared AVS2 renderer and waveform, and
plays via AppKit/display-link and AVAudioEngine. No browser, WebRTC, server
pipeline or cloud build is involved. Use the green window button for full
screen. Stop/restart on audio-device changes; use built-in speakers for the
comparison. The default build is ad-hoc signed; optionally set
`AVSYNC_SIGN_IDENTITY` to an existing local signing identity.

Bonjour `_avsync-peer._udp` advertises an ephemeral pairing code, not the device
name. Eight short UDP exchanges carry request nonce, receiver send time and
sender receive/send times. At least three consistent replies are required;
minimum network interval wins. The source freezes one UTC/monotonic mapping
for both scheduled AVS2 timestamps and clock replies. The receiver maps its
own monotonic time into that sender mapping, so neither needs accurate UTC
for this **relative** test. The display includes a network/stamping interval,
100-ppm two-clock holdover allowance, five-minute expiry and unknown physical
accuracy. This interval is an estimate, not a guaranteed bound on asymmetric
LAN delay. Neither device system clock is changed.

Wrong origin, oversized/invalid packets, self-sync, clock/session changes and
disagreeing samples are rejected. Correction is applied only to matching
source-ID and TAI-offset reports; switching sources cannot silently reuse it.
Background/settings changes invalidate acquisition; background invalidates
the retained in-memory reference. The protocol is unauthenticated, intended
for an explicitly selected sender on a trusted test LAN. No camera/audio
recording or saved measurement is transferred. Pairing does **not** determine
display, speaker, camera or microphone latency; these remain in the result.

Checks: native packet and real UDP-loopback tests, source-bound JS correction,
renderer/QR/waveform tests and local iOS builds. Physical device-to-device
accuracy and comparison against the WebRTC path require the field run.

## Acoustic two-way calibration (C2/C3 spike)

Status: design, plus the maths and the onset extraction in the phone core
(`core/include/avsync/two_way.hpp`, `core/src/two_way.cpp`). They are tested
with simulated recordings, not yet with real devices. The device parts are
listed under "Next steps".

## Why

A phone's measured AV offset and latency include its own output (speaker,
screen) and input (microphone, camera) latencies, and its clock offset. Clap
calibration gives one sum. Two devices that measure each other can separate
more of it, without a lab clock.

## The two exchanges

Both use the existing AVS2 signal. The audio packet carries a source id and
the emission time on the emitter's own clock (`audio_send_ns`), and every
reception time refers to the chirp onset.

1. **Two-way acoustic ranging (C3, BeepBeep).**
   - A plays one packet and B plays one packet, a few hundred ms apart. Both
     record the whole time.
   - Each device finds, on its own clock, the onset of its own packet (own
     speaker → own mic) and of the other's packet
     (`onsets_from_recording`, which tells packets apart by source id).
   - The devices exchange these two local numbers (QR on screen, network, or
     the operator).
2. **Cross measurement (C2).**
   - A generates; B measures arrival − the emission time that the packet
     carries. Then the roles swap.
   - Each device also measures its own loopback: own onset − own emission
     time.
   - This is the same recording as in 1. With AVS2 one combined run gives
     both.

## Maths

- **Notation.** `θ` = clock B − clock A; `out` and `in` = a device's output
  and input latency; `d` = distance between the devices; `d_AA`, `d_BB` = each
  device's own speaker-to-mic distance; `c` = 343 m/s.
- **What the two recordings show** (each on its own clock, `T_A` and `T_B` =
  emission times):
  - `a.own   = T_A + out_A + d_AA/c + in_A`
  - `a.other = T_B + out_B + d/c    + in_A`
  - `b.other = T_A + out_A + d/c    + in_B + θ`
  - `b.own   = T_B + out_B + d_BB/c + in_B + θ`
- **Distance.** Emission times and output latencies cancel in the sum:
  `(a.other − a.own) + (b.other − b.own) = 2d/c − (d_AA + d_BB)/c`, so
  `d = c/2 · Σ + (d_AA + d_BB)/2`.
- **Clock.** `((b.other − a.own) + (b.own − a.other))/2 = θ + (in_B − in_A) + (d_BB − d_AA)/2c`.
  Acoustics give the clock offset **plus the input-latency difference**, never
  `θ` alone. This is reported as `clock_offset_plus_input_skew_ms`.
- **Cross runs.**
  - The two measurements: `m_AB = out_A + d/c + in_B + θ` and
    `m_BA = out_B + d/c + in_A − θ`.
  - Sum, with the path removed: `out_A + in_A + out_B + in_B`. This round trip
    is free of the clock. It must equal the two loopbacks with their self
    distances removed; `consistency_ms` checks this.
  - Half the difference: `θ + ((out_A + in_B) − (out_B + in_A))/2`.
- **What can be solved.** There are 5 unknowns (`out_A`, `in_A`, `out_B`,
  `in_B`, `θ`) but the four equations (two loopbacks, two cross
  runs) have rank 3: their sums obey the consistency relation above.
  Two degrees of freedom remain (the distance comes from the ranging):
  - the split of each device's loop into output and input, together with
    `θ`;
  - with a **trusted clock** (C4 SNTP/PTP gives `θ`) and **one known latency**
    (a reference device, or a speaker with a known output delay such as the
    platform's ST 2110 network speaker), all four follow (`device_cross(…,
    theta, in_a_reference)`).
  - Two phones alone give the distance, the loopback of each device, the
    round trip and `θ + skew`. That is enough for a relative calibration: the
    same two devices measure the same chain consistently.

## Accuracy

- **Simulation** (unit tests, 48 kHz, integer-sample placement, with and
  without white noise of ±0.02 amplitude):
  - distance error 0.3 mm;
  - `θ + skew` error 0.007 ms;
  - round trip 0.03 ms;
  - split latencies within 0.011 ms.
- **Expected limits on real devices:**
  - microphone AGC and noise suppression: iOS `measurement` mode is needed;
  - the speaker's onset is smeared by the room (reflections), maybe 0.1–0.5 ms;
  - sample-clock drift between the devices, about 20 ppm: 0.02 ms per second
    between the two emissions. Keep the emissions close, or estimate the drift
    from a second pair;
  - temperature: 0.6 m/s per °C gives 0.2 % of the distance.

## Next steps (devices needed)

1. iOS maths is now compiled through `Core_two_way.cpp` and exposed by the
   experimental `solveDeviceCalibration` module function. Recording acquisition
   and onset extraction into the device-exchange workflow remain to be wired.
2. App flow:
   - pair the two phones;
   - each plays its packet at a scheduled time and records 3 s;
   - exchange the two onsets (QR shown on screen, or a local network message);
   - show the distance, the loopback of each device and `θ + skew`.
3. Audio session: `AVAudioSession` `measurement` mode, built-in route only,
   and repeat the run on a route change (C6).
4. Video (camera ↔ screen) uses the same algebra with flash onsets. The
   display adds 1–4 frames of jitter (C7).
5. Combine with C4: with a trusted `θ`, the skew (in_B − in_A) follows. With a
   reference latency, the per-device split follows.
6. Field test: two iPhones at 0.5, 1 and 3 m, and a tape-measured distance as
   the check.
