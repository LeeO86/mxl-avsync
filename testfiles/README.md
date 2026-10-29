# AV-sync test clips

Curated clips generated from this repository's actual test-picture renderer: color bars, TEST SIGNAL banner, QR payload, four corner fiducials, Gray-code strip, timing marker and audible chirps. The named offset is **audio time minus video time**: positive means audio is late; negative means audio is early.

## Files

- `1080p50_h264_aac_sync.mp4`: 1920×1080, 50 fps, H.264/AAC, **0 ms**, 10 seconds. Start here.
- `1080p50_h264_aac_audio_late_100ms.mp4`: same format, audio **+100 ms late**, 10 seconds.
- `1080p50_h264_aac_audio_early_100ms.mp4`: same format, audio **−100 ms early**, 10 seconds.
- `1080p25_h264_aac_sync.mp4`: 1920×1080, 25 fps, H.264/AAC, **0 ms**, 10 seconds.
- `1080p59.94_h264_aac_sync.mp4`: 1920×1080, 60000/1001 fps, H.264/AAC, **0 ms**, about 10 seconds.
- `720p50_prores_pcm24_sync.mov`: 1280×720, 50 fps, ProRes 422 HQ and 24-bit PCM, **0 ms**, 6 seconds.
- `1080p50_h264_pcm16_audio_late_35ms.mkv`: 1920×1080, 50 fps, H.264 and 16-bit PCM, audio **+35 ms late**, 10 seconds.
- `2160p50_hevc_aac_sync.mp4`: 3840×2160, 50 fps, HEVC/AAC, **0 ms**, 6 seconds.

MP4 video uses 8-bit YUV 4:2:0 for browser/iPhone compatibility; HEVC uses the Apple-compatible `hvc1` tag. MOV/ProRes and MKV/PCM are professional/desktop reference variants and may need an appropriate player. Audio is stereo 48 kHz. Event rate is nominally 2/s and aligned to the video-frame grid: 25 fps uses a 520 ms period, and 59.94 fps uses about 500.5 ms.

## Test the phone app

Play the 0-ms 1080p50 clip fullscreen on the screen/speaker path under test. Keep all four corner markers visible and the chirps audible. Then compare +100 and −100 ms using the same player, display and audio route. The reading should change by the known difference; the absolute phone reading also contains the real playback-path AV offset and capture bias. Loop boundaries require the event tracker to reacquire the reset sequence.

## Verification and regeneration

`index.json` lists actual codecs/raster/rate/duration, SHA-256 hashes, expected offsets and decoded measured offsets. Each clip has an adjacent `.manifest.json` with event identities/times. `verification/` contains the actual analyzer reports. These are file-signal checks, not independent validation of phone capture accuracy or playback latency.

Build only the portable file tool (avoids requiring the Linux live-service dependencies on macOS):

```sh
cmake -S . -B build/testfiles -G Ninja -DCMAKE_BUILD_TYPE=Release -DAVSYNC_BUILD_TESTFILE_TOOL=ON
cmake --build build/testfiles --target avsync-testfile-tool -j 6
python3 testfiles/generate.py
# Replace these named curated clips after changing the generator:
python3 testfiles/generate.py --force
```

Requires CMake/Ninja, a C++20 compiler, FFmpeg development libraries, Python 3.11+, and ffmpeg/ffprobe with libx264/libx265. Pinned Blend2D and ZXing are fetched by the existing build.

Generation starts from a ProRes/PCM reference, then encodes delivery formats with FFmpeg and verifies the final decoded AV offset (at least three valid events, error ≤2 ms). This avoids a reproduced direct H.264/AAC generator-path shift of about −18.7 ms with FFmpeg 8.1.1 on this Mac. Container timestamp checks from the intermediate reference are explicitly superseded for transcoded files; the final ffprobe metadata and measured results are the evidence. An ordinary Git fixture is limited to less than 95 MiB per file by the generation script.
