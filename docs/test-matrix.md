# Test matrix

Measured on the cloud build VM (x86-64, no PTP, no ST 2110 NIC) unless a row says otherwise. Positive offset means audio is late. Uncertainty on the clean PCM path is far inside 0.1 ms; the published audio uncertainty used by results is 0.10 ms at correlation ≥ 0.95 so that it also covers the band-pass row.

## Unit (avsync-core)

| Condition | Detection | Timing p99 | Notes |
| --- | --- | --- | --- |
| Clean PCM 48 kHz, −12 dBFS | 100 % | ~0 ms | ≤ 0.1 ms target |
| Level −40 dBFS and −1 dBFS | 100 % | ~0 ms | |
| White noise SNR 20 dB | 100 % | 0.0007 ms | |
| White noise SNR 10 dB | 100 % | 0.0010 ms | ≥ 99 % target |
| White noise SNR 0 dB | 100 % | 0.0058 ms | |
| Compressor / AGC | 100 % | 0.0031 ms | |
| Band-pass 300–3400 Hz | 100 % | 0.054 ms | still inside 0.10 ms |
| Resample 48 → 44.1 → 48 kHz | 100 % | 0.00004 ms | linear resampler in the test |
| G.711 μ-law and A-law | 100 % | ≤ 0.00007 ms | table codec |
| 10 ms gap in the chirp | 100 % | 0.00009 ms | |
| Second chirp 40 ms later | 100 % | ~0 ms | no cross-confusion on the clean path |

Chirp shapes, 30 ms, 700–2500 Hz and threshold 0.5 are unchanged: every row above cleared the threshold.

## File integration

| Path | Result |
| --- | --- |
| MOV + ProRes 422 HQ + PCM 24-bit, 1080p50, offset 0 | median ~0 ms, container time bases 1/50 and 1/48000, checks pass |
| Same, 720p50, offset +35 ms | median +35.0 ms, deviation ~0, grade pass (graded on deviation) |
| Same, offset −35 ms | median −35.0 ms, deviation ~0 |
| Offset sweep −40,−20,0,20,40 | every event's deviation within 1 ms |
| ffmpeg `adelay` +20 ms on the offset-0 file | median +20.0 ms |
| MP4 + H.264 + AAC, 720p50, offset 0, `--allow-container-shift` | container shift is reported; measured median about −19 ms on this clip because of AAC priming and H.264 B-frames. Not the default. |

## Not measured here

| Path | Why |
| --- | --- |
| MXL generator → analyzer on one node | this binary does not link libmxl; `/readyz` stays 503 until a build with libmxl creates the flows |
| ST 2110 loop, 1 h at 1080p50 and 1080i25 | needs the Linux host, PTP, and mxl-st2110-gateway |
| SRT, cross-node fabrics, live G.711 | stage 2 |
| Phone at a monitor | stage 3, after 1.0 |
