# Audio timecode and onset flash

The default generator uses a known 30-ms 700–2500-Hz up-chirp followed by
voice-band timecode at **1200 baud**. At zero source offset, the white marker
starts on the same video event as the **start of the chirp**. The marker stays
white for 100 ms, rounded up to whole video grains, then returns to black.
There is one white pulse per event. Positive source offset still means audio late.

Choose **1 or 2 events/s** in the web **Signal** panel and press Apply. Both
selectors retain drafts across polling and support Discard. The equivalent
settings are `AVSYNC_EVENT_RATE=1|2` and `AVSYNC_AUDIO_TIMECODE=1200`.
`AVSYNC_AUDIO_TIMECODE=600` is an optional slower profile requiring 1 event/s.
`legacy` retains the original four chirp shapes, centre reference, alternating
marker and 1/2/4 events/s. Old recordings/QRs do not silently acquire a new reference.
Select legacy when feeding an older analyzer; the new flash/data signal requires
an updated receiver.
The timecode prefix has fixed duration/band; custom chirp settings require legacy.

File example:

```sh
build/mxl-avsync generate --format 1080p50 --duration 10 --event-rate 2 \
  --audio-timecode 1200 --offset 0 --container matroska \
  --video-codec h264 --audio-codec pcm16 --out /tmp/signal.mkv
build/mxl-avsync analyze /tmp/signal.mkv --json /tmp/result.json
```

## Protected compact payload

All multibyte integers use big-endian byte order. This is a custom modem using
1300/2100-Hz continuous-phase binary FSK, not a V.23 compliance claim.

- Version/flags: 1 byte, `0x30 | LIVE(1) | zero-offset(4)`. Version 3 always
  references chirp onset. FILE timestamps are media time; LIVE timestamps are TAI.
- Source ID: 4 bytes, CRC-32/IEEE of the UTF-8 test ID. Use distinct test IDs for
  unrelated sources. It intentionally remains identical across paths from one source.
- **Common video-event timestamp**: 6 bytes, unsigned Q32.16 TAI/media seconds.
  Resolution is 15.259 µs, maximum rounding error 7.630 µs; LIVE range ends in 2106.
- Source audio-minus-video offset: signed 2 bytes in units of 1/16 ms. Maximum
  rounding error is 31.25 µs, range −2048 to +2047.9375 ms. This limits the source
  offset field, **not path latency**. When the protected zero-offset flag is set,
  these two bytes are absent; the packet is independently decodable without history.
- CRC-32/IEEE: 4 bytes over the preceding payload.

The decoder reconstructs audio-send time as common event time plus source offset.
Source ID plus common event timestamp identifies the event without a repeating
two-bit chirp ID, truncated sequence or a previous packet. Combined timestamp
quantization is below 0.04 ms. This does not establish capture/clock accuracy.

A 10-ms guard follows the chirp. Data framing is 32 alternating preamble bits,
sync word `0x1ACFFC1D`, K7 rate-1/2 convolutional coding (0x79/0x5B), six zero tail
bits, permutation `73*i mod coded_bits`, and soft Viterbi decoding with CRC gate.
17-byte packets use 284 coded bits; 15-byte packets use 252. Full durations
**including chirp and guard** are 330 / 303.333 ms at 1200 baud, and
620 / 566.667 ms at 600 baud. The receiver blindly tries both rates and lengths.
It also reads the experimental 25-byte `0x21` centre / `0x23` onset packets.

The main generator, file analyzer, live analyzer and native iOS module link the
same `core/src/timecode.cpp`. The old probe remains as a historical experiment;
test the actual shipped codec with `tests/audio-timecode/run.py --production`.

## Timing, pairing and streaming

The matched filter locates the chirp centre internally. A valid onset packet
reports the corresponding start, while historical centre packets retain their
original reference. The receiver stores the arrival-clock mapping of the chirp's
samples. FSK/FEC/CRC completion never becomes the measurement arrival time.

QR extensions `tc:"v3"` and `sid` identify the flash format/source and true grain
period. New FILE QRs carry `tk` in media nanoseconds; historical FILE QRs retain
their millisecond meaning. TAI values are parsed as exact integers. Picture
observations get their common event identity from the QR and full strip sequence;
audio reconstructs it independently from its packet. Coded events are paired by
identity rather than the legacy ±2-period window. Online pairing keeps a bounded
10-second waiting window; this is an implementation retention limit, not modulo
ambiguity. Different source IDs or unverified legacy chirps cannot fill a coded pair.

The audio writer latches complete telegrams. Changing an offset cannot rewrite
one already being transmitted. A colliding live event is skipped, counted in
`mxl_avsync_timecode_packets_skipped_total` and shown in status. Invalid FILE
offset sweeps that overlap telegrams are rejected. Rate/profile changes start a
new schedule; receivers reacquire the new QR/period. Transitional packets can be
lost, so measure after reacquisition. File plans omit tail events that cannot
fit a complete packet in the requested duration.

## Phone feedback and latency

Gray means absent, orange means a detected chirp without decoded timecode, and
green means a CRC-valid audio timecode. Picture feedback and its detected outline
remain independent. The lease accounts for the chosen packet duration, so a
valid 1-Hz source does not flash gray between packets. AV stability windows also
cover enough events at 1 Hz and still expire when current picture tracking stops.

A LIVE audio packet can provide **estimated audio latency without picture/QR**.
This compares its TAI send time with capture timestamps mapped to device UTC,
using the configured TAI−UTC offset. Device/source clock accuracy and capture
delay remain unknown; HTTPS clock checks are advisory, never automatic clock lock.
Video estimates use matching LIVE QR metadata. FILE packets expose timecode and
relative arrival, not wall-clock latency. AV measurement still works uncalibrated
with 0-ms correction. Calibration does not correct absolute per-path estimates.

The file CLI accepts audio-only timecoded WAV/media and reports independently
decoded packets. A recording's media PTS is not a trusted receiver wall clock,
so this does not invent absolute latency for recorded LIVE packets.

## Validation

```sh
ctest --test-dir build --output-on-failure
node --test mobile-app/src/*.test.mjs tests/web/*.test.cjs
python3 tests/integration/timecode.py build/mxl-avsync
python3 tests/audio-timecode/run.py --production --packets 128 --seed 20261029 --out /tmp/timecode-channels
# Mac: Xcode and a booted arm64 iOS simulator
AVSYNC_TESTFILE_TOOL="$PWD/build/avsync-testfile-tool" python3 mobile-app/tests/native/run.py
```

The last command optionally tests rendered FILE QR recognition through Vision;
the native suite always checks compact LIVE signals at both event rates,
injected +35-ms AV delay, +120/+155-ms per-path clock estimates, and audio-only
decoding with no image or QR. These are synthetic/codec regressions. Actual
speaker/microphone, telephone noise suppression, real G.711 calls and device
timing accuracy still require hardware tests. Opus file coding is not WebRTC
transport validation. Android native capture remains unsupported.


[Recorded local integration results](../tests/audio-timecode/RESULTS-2026-10-27-COMPACT.md).
Camera and file QR decoding share a Vision reader with a compatible revision-2
fallback; the simulator uses its CPU path. The native tests exercise the actual
rendered QR, not only injected metadata. Set `AVSYNC_NATIVE_OUT` to an empty
directory to retain synthetic simulator artifacts for debugging.
