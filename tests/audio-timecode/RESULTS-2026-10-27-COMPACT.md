# Integrated compact codec — 2026-10-27

This tests the production core implementation, not the historical 25-byte
prototype. Source/binary/media hashes and all counts are in
[the full matrix](results-2026-10-27-compact.json).

```sh
python3 tests/audio-timecode/run.py --production --out /tmp/avsync-compact-final --packets 128 --seed 20261029
```

93 rows cover 1200 baud at 1/2 Hz and 600 baud at 1 Hz. Every complete packet
decoded in all 25 non-extreme-noise positive conditions per profile. No wrong
payload was accepted; all wrong-CRC/legacy/noise/tone controls stayed empty.
The independent Python struct/zlib vector checks the actual 17-byte wire
layout. The source streams include both 15-byte zero-offset and 17-byte packets.
CRC/prefix/correction counters are not exported by this production decoder;
the correction field's placeholder is not a measured correction count.

At 0-dB noise, 1200/1 Hz decoded 90/128 and 1200/2 Hz 80/128; 600/1 Hz decoded
128/128. At −6 dB every profile decoded 0/128. These finite trials and changed
payload/seed do not isolate a baud/protocol effect or prove call robustness.
Maximum accepted anchor shift among PCM/phone-filter G.711 rows was below
0.069 ms on the synthetic timeline. This is not physical capture accuracy.

Additional local validation:

- Core/picture CTest passed, including exact TAI parsing, one flash per event,
  packet block invariance, collisions, identity pairing beyond the old window,
  mixed compact lengths, CRC rejection, and pre-completion arrival retention.
- 30 phone/web JavaScript tests passed, including rate/profile draft persistence.
- `tests/integration/timecode.py` passed full generation/encoding/blind analysis
  for 1/2 Hz, 59.94 fps, 600 fallback, and legacy. Extracted G.711 audio with a
  +3217-ms delay retained exact source/event identity without manifest or image.
- Native iOS simulator suite passed compact LIVE at both rates, +35-ms AV,
  independent +120/+155-ms clock estimates, audio-only without picture/QR,
  and actual rendered FILE QR through the shared Vision reader. Existing native
  orientation, signal-loss, unstable-pair and calibration regressions passed.
  The final suite also publishes +3217-ms AV and +3337-ms estimated audio latency;
  a late matching side is not dropped by the short UI statistics window.
- Digital and established camera locator paths both pass. A merged digital-only
  search was caught by replaying a previously readable local camera take; the
  fallback restores recognition. A new public full-renderer exposure/softening
  test passes alongside the existing digital tile regression. The private take
  remains local and provides no independent offset-accuracy reference.

The rendered QR exposed a Vision compatibility issue: the simulator's newest
request initially lacked an inference context; CPU revisions 3/4 returned no
barcode on the valid image, while 1/2 and Core Image decoded it independently.
The camera and file analyzer now share a reader using a compatible revision-2
fallback, with a simulator CPU path. Detection thresholds were not loosened.

Linux MXL/NMOS/AMWA and actual pipeline checks belong to the peer-host handoff.
Physical iPhone speaker/microphone, actual calls/noise suppression, and hardware
clock accuracy remain unverified. No merge, tag or release is implied.
