# Experimental audio-only timecode modem

This is an offline encoder/decoder and channel benchmark, **not the production
generator or iPhone integration**. It compares 1200 baud (preferred) against a
600-baud fallback. No image, QR, manifest, expected packet count, timestamp or
source ID is supplied to the decoder. It finds the chirp, tries both baud rates,
and accepts a packet only after FEC decoding and CRC-32 validation.

## Reproduce

Requires a C++20 compiler, Python 3 standard library and FFmpeg with G.711 and
libopus. No Python packages, GPU, live MXL/NMOS service or internet connection
are required after these tools are installed.

```sh
python3 tests/audio-timecode/run.py --out /tmp/avsync-timecode-results --packets 128 --seed 20261026
```

The script compiles the real native probe, checks its wire bytes independently
with Python struct/zlib, runs FEC/CRC self-tests, creates mono 48-kHz source WAVs,
and passes them through **FFmpeg's actual 8-kHz G.711 A/µ-law codecs**, telephone
filters, controlled impairments and Opus. The analyzer receives only an 8-kHz
PCM16 WAV. `results.json` records source/binary/WAV hashes, exact accepted counts,
incorrect packets, CRC rejections, timing shifts and runtime. Detailed decoded
packets, manifests and media stay in the requested output directory.

## Event rate and timing reference

The probe's `generate` command accepts `--event-rate 1` or `--event-rate 2`.
1200 baud defaults to 2/s; either rate is supported. The 600-baud full packet
defaults to 1/s and rejects 2/s instead of overlapping telegrams. The compatible
`--period-ms 1000|500` option remains; contradictory options are rejected.
The dedicated compatibility test checks both references at both rates through
G.711 A/µ-law with a known +35-ms arrival delay, a mixed-reference recording,
and rejection of invalid/overlapping configurations. After compilation:

```sh
python3 tests/audio-timecode/test_reference.py /tmp/avsync-timecode-results/audio-timecode-probe
```

For example, after the runner compiles the probe:

```sh
/tmp/avsync-timecode-results/audio-timecode-probe generate --baud 1200 --event-rate 1 --output /tmp/one.wav --manifest /tmp/one.json
/tmp/avsync-timecode-results/audio-timecode-probe generate --baud 1200 --event-rate 2 --output /tmp/two.wav --manifest /tmp/two.json
```

`--anchor centre` explicitly reproduces the old candidate. The receiver identifies
the reference from CRC-protected packet flags; old audio timestamps do not silently
move by 15 ms. Run the full benchmark with `run.py --anchor centre` for that format.
The dated 2026-10-26 results and listening examples retain their historical centre
reference and hashes. These changes do not switch the released generator or app.

For the new live format, zero source offset means **the visual event and chirp
start coincide**; positive offset still means audio late. A white flash should
start at that visual event. This audio-only harness has no video track. The legacy
generator alternates its marker black/white, so changing it to a white pulse on
every event also needs coordinated picture-tracker integration. The proposed
17/15-byte compact payload has not yet replaced this tested 25-byte candidate.

## Candidate framing

- Known 30-ms 700–2500-Hz up-chirp, with its **start as the measurement anchor** (new default). The matched filter
  still finds the centre internally; the receiver subtracts half the chirp length.
  The protected header is `0x23` for start, `0x21` for historical centre. Both
  decode without supplying a reference setting to the receiver.
- 10-ms guard after the chirp, then continuous-phase binary FSK at 1300/2100 Hz.
- 32 alternating preamble bits and a fixed 32-bit sync word.
- 25 payload bytes, big-endian: version/flags (1), source/session ID (4), event
  sequence (4), actual audio-anchor send time in TAI nanoseconds (8), signed
  audio-minus-video source offset in nanoseconds (4), CRC-32/IEEE (4).
- K=7, rate-1/2 convolutional coding (0x79/0x5B), six zero termination bits,
  permutation interleaver 73 modulo 412, soft-decision Viterbi decoding.
- Full packet identity is source + sequence + 64-bit timestamp. The fixtures
  cross the legacy 24-bit sequence wrap and include offsets beyond one second.

There are **476 FSK symbols** per packet. Total duration is 436.667 ms at 1200
baud, and 833.333 ms at 600 baud. The measurement anchor precedes data completion
by 436.667 / 833.333 ms respectively (421.667 / 818.333 for historical centre); packet-decoding time is **not** used as the
measured arrival time. 1200 baud fits two complete packets/s; 600 baud is tested
at one complete packet/s. Fragmentation to retain two markers/s at 600 baud is
not implemented. Both rates use identical tone frequencies for a controlled
comparison: this is a custom experiment, **not a conformant V.23 modem**.

Decoder timing search and rate hypotheses are demodulation parameters, not a
measurement of the receiver's clock accuracy. Source timestamps in WAV fixtures
are synthetic and fixed for reproducibility; playing a fixture later does not
provide a real wall-clock latency measurement.

## What is tested

The paired 1200/600 comparison uses the same event spacing, payload contents and
seeded impairment generation. An additional 1200-baud stream tests two events/s.
Conditions include PCM, G.711 A/µ-law, 300–3400-Hz filtering, reduced level,
compression/AGC, clipping, 44.1-kHz resampling, ±500-ppm clock-rate changes, Opus
16/32 kbit/s, AWGN at 20/10/6/0/−6 dB, two synthetic echoes, 10/20/40-ms erased
data bursts, a repeated 20-ms PLC-like segment, 3217-ms delay and a random join
in the middle of a stream. Loss positions affect only the channel, never the
receiver's knowledge. SNR is referred to the nominal steady FSK tone RMS; noise
is added after the phone/µ-law path. The combined case adds 6-dB AWGN and a
12-ms, 0.2-gain delayed copy.

Negative controls are complete FEC-valid packets with deliberately wrong CRC,
legacy four-ID chirps, white noise and unrelated steady tones. A successful
benchmark exit requires zero incorrect accepted payloads, zero packets in the
negative controls, and complete decoding of PCM/basic G.711/phone-filter rows.
Stress losses are reported **without being converted into a false overall
robustness pass**. These finite trials do not prove a false-acceptance rate.

Anchor-shift statistics compare accepted chirp timestamps against the injected
sample timeline. They include channel filter/echo effects and exclude rejected
packets. Always read them together with the accepted count: a small error among
a few surviving packets does not establish robust detection.

## Remaining boundaries

No real telephone call, acoustic speaker/microphone, iPhone audio-only decoder,
WebRTC transport, live generator scheduling, neural noise suppression or
physical UTC/TAI accuracy is established by this benchmark. Opus file coding
is not an end-to-end WebRTC test. The current app and released generator still
need integration of the chosen format before they can measure these packets.
The legacy ±2-period AV pairer is unchanged. The offset field is verified as
data, but there is no video track or real offset-sweep scheduler in this test.
At 1200 baud only about 63 ms remain between full packets at two events/s;
changing per-event source offsets can cause telegram overlap. Live integration
must explicitly handle spacing, overlaps, source changes and longer identity-
based pairing rather than inheriting the old repeating-ID time window.


## Recorded comparison

[Results from 2026-10-26](RESULTS-2026-10-26.md) contain the frozen 128-packet
comparison and link the full JSON matrix. Successful decoder output requires
an exact CRC-valid payload; stress-case losses remain visible in the report.

[Start-reference/rate validation from 2026-10-27](RESULTS-2026-10-27.md)
records the new default, historical compatibility and dedicated G.711 timing checks.
