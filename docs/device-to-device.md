# Device-to-device calibration (C2/C3 spike)

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
  `in_B`, `θ`) but only 4 independent equations (two loopbacks, two cross
  runs; the distance comes from the ranging). One quantity stays open:
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

1. iOS: add `Core_two_way.cpp` to the module and expose `onsets_from_recording`
   and the two functions to Swift (`AVSyncStreamAnalyzer`).
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
