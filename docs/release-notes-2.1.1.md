# mxl-avsync 2.1.1

Fixes from the first phone field tests of 2.1.0, mostly for the phone app.
Ports, environment, NMOS resources, metrics and dependency pins are
unchanged from 2.1.0.

## Audio timecode in rooms with echo

- **Why.** A phone recording in a room found every start chirp but decoded no
  packet. The speaker and room made 2100 Hz arrive 17 dB below 1300 Hz, with
  echo only 8 dB below the direct sound. That fails even without noise, so a
  louder signal did not help.
- **What changed.** A packet that fails as received is decoded again through
  an equalizer: a 96-tap filter at 8 kHz, fitted to the start that every packet
  shares (chirp, guard, preamble, sync). The recording went from 0 to 27 of 27
  packets. Timing still comes from the chirp as received, so AV and latency do
  not move. Clean (electrical) audio decodes as received, as before.
- The live decoder no longer decodes a burst again on every scan once it is
  decoded or complete, so it costs less CPU than 2.1.0.
- Details in [audio-timecode.md](audio-timecode.md).

## Phone app

- Video latency is shown again for a platform (PTP) source; it was only shown
  for the phone's own generator.
- The clock is checked again as soon as its holdover allowance passes the
  limit (at most every 30 s), not only after 4 minutes.
- Settings shows the NTP accuracy and its age; Debug shows the last six audio
  bursts (decoded, through the echo filter, chirp match, sync errors of 32,
  level) and, on iOS, the microphone gain.
- Shipped as TestFlight Mobile 2.0.8 build 12. It reads AVS3, so it matches
  2.1.x generators.

## What changes for the platform

- In-place upgrade; no settings change. 2.1.0 and 2.1.1 instances work
  together (same optical code and packets).
- The analyzer's audio decoder gains the echo filter (helps acoustic paths,
  for example a microphone in a room). No event, metric or field is new,
  renamed or removed.
