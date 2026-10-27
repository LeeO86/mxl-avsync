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
| G.711 μ-law and A-law | 100 % | ≤ 0.00007 ms | table codec, test aid only |
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

## File-mode test aids (this VM)

H.264 and G.711 rows below are FFmpeg test aids. They are not live SRT or SIP inputs.

| Path | Result |
| --- | --- |
| Fiducial search, picture offset in a larger frame and scaled 2× | sequence and marker state recovered (`avsync-core-tests`) |
| 720p50 ProRes scaled to 640×360 and padded into 960×540, `--locate` | median ~0 ms, 2 events |
| H.264 at 1, 4 and 10 Mbit/s, PCM audio kept | median ~0 ms at each rate. Test aid only |
| G.711 PCMU and PCMA at 8 kHz, video copied | median within 0.1 ms. Test aid only |
| Merge of two `tai-ptp` results 40 ms apart on arrival time | end-to-end 40.0 ms. This repo merges results; it does not move the flows |

## Not measured here

| Path | Why |
| --- | --- |
| MXL generator → analyzer on one node | measured on this VM, 1080p50, 2/s, tmpfs domain. Set 35 / 0 / −35 ms read back as 35.0002 (warn), 0.0002 (pass) and −35.0005 (warn). 1080i25 grain is one 540-line field. No latency: PTP state was not read |
| ST 2110 hour run, 1 h at 1080p50 and 1080i25 | not this repo. Through mxl-st2110-gateway: generator → MXL → gateway → network → gateway → MXL → analyzer |
| SRT | not this repo. mxl-srt-gateway presents an MXL flow. The H.264 rows above are file-mode test aids |
| Cross-node fabrics | not this repo. mxl-fabrics-agent moves the flows. The merge view here runs on saved results |
| SIP / G.711 live | not this repo. A sibling gateway presents the audio as an MXL flow. File-mode PCMU/PCMA is a test aid |
| Physical phone emission/capture accuracy | Native iOS capture/generator and simulator tests are implemented; independent LED/piezo/photodiode validation and Android capture remain pending |
| PTP-locked latency (`video_latency_ms`, `audio_latency_ms`, index shifts, `av_offset_arrival_ms`) | lab only. This VM does not lock PTP, so those fields stay absent |
| A registry on a real network | lab only. Registration here is a local stand-in, or the AMWA job's registry container |
| Crosspoint-driven routing | lab only. This VM does not drive an IS-05 crosspoint |


## Native phone source / optical live analyzer (2026-10-27)

The native UIKit generator test decodes the actual generated QR at 1/2 Hz and
checks every 60-Hz strip/flash phase against the shared core reader. Its actual
audio waveform independently recovers source/time plus the protected estimated
flag. The 8-kHz A/µ-law unit paths also recover every phone packet. Existing
native capture/calibration regressions, 30 JS tests and full simulator/device
builds pass. Simulator UI checks cover Stop/Close/restart and inactivity.

The Linux MXL regression (`tests/integration/phone_generator.py`) carries a
720p60 source in a 1080p50 camera raster, with 120-ms video and 155-ms audio
index delays. One observed run recovered 18/37 valid pairs at 1/2 Hz, median
+34.992 ms; no video events appeared during a three-second image loss, and
pairing resumed after recovery. Reader regressions cover both 1080p and 4K
search downscaling. The normal direct MXL regression and all CTest suites pass.

These are synthetic/native software results. Phone-source latency remains
explicitly **estimated**, even with a locked receiver, and optical uncertainty
is unknown. A software display schedule and reported audio output latency do
not measure photons/sound emission, independently calibrate a camera, or prove
physical call-path accuracy. See [workflow and reproducible tests](phone-generator.md).
