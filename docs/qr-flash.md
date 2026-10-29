# QR + flash signal

AVS2 replaces the compact source's corner finder and Gray strip with one large
identified QR and a neighbouring flash panel. All source types use the same
layout. Legacy signals remain supported by their original detector.

The QR holds the **current** event until the next flash. Its payload contains
source, full event sequence, full video send timestamp, expected audio offset,
source format/rate and FILE/LIVE/estimated-clock flags. It is a fixed 59-character
alphanumeric message in a version-4 Q QR. Names, subtitle and image remain in
the visible layout, outside the machine payload. Audio remains the independent
1200/600-baud CRC/FEC timecode; it does not need the picture to decode.

QrFlashTracker receives QR capture coordinates/time separately from decoded
content delivery. It samples static QR finder references and a small central
flash grid, with no raster rectification. It retains the first confirmed white
frame's index, arrival and source identity; decoding the QR later cannot date
the flash later. No source-time prediction fabricates a missing pulse.

Only the QR quiet zone and flash field need survive a crop. External corners,
colour bars and bottom strip are not needed for detection. Motion, occlusion,
expired geometry and ambiguous transitions withhold new events. Frozen video
cannot produce new video events. Per-frame drops/duplicates are no longer
measured for AVS2; use transport diagnostics for those. Estimated phone
latencies include unknown clock/display/speaker/camera/microphone errors.

## Reproduce

```sh
cmake --build build --target avsync-core-tests avsync-picture-tests
ctest --test-dir build --output-on-failure
python3 tests/integration/phone_generator.py --help
cd mobile-app
npm test
npm run test:ios-generator
npm run test:ios-native
```

Use a local signed development build and Metro for physical iPhone tests;
TestFlight is unnecessary. Back up existing preferences and private source
images before installing, then verify saved results and calibration afterward.

For AVS2 identity, up to 32 sparse 33×33 module-center samples are retained
from the start of each pulse. The later decoded QR is re-encoded using the
same fixed version/ECC/mask-selection rules; at least 80% confidently sampled
modules and zero contradictory confident bits are required in one sample.
Unknown/transition modules are erased, not corrected by a permissive timing
window. A QR already observed before the pulse cannot identify a new pulse.
This prevents frozen QR or skipped-event callbacks from relabelling an edge.
Nayuki is vendored into the portable core for this check, with no new external
media/GUI dependency. Sampling occurs only within the first 100 ms of the pulse and stops after identification. This allows QR pixels to settle after the flash has already started.

## Validation on 2026-10-29

- macOS Core/Picture CTest and Linux Core/Clock/Picture CTest passed.
- Actual generated 1-/2-Hz ProRes/PCM and 1-Mbit H.264/PCM files, including a
  crop retaining only QR/flash, 90-degree rotation and a scaled camera tile:
  5/10 pairs per six-second clip at +34.9925 ms for a +35-ms source. No picture
  controls produced zero pairs. No manifest supplies the identity.
- Apple's actual QR decoder and native file/live pipelines passed for both
  rates, cropped/rotated video, independent audio identity, delayed QR delivery,
  +3217-ms pairing, estimated latency and calibration regressions. The focused
  high-FPS test also covers 42-ms QR pixel settling after the optical onset.
- Linux MXL synthetic phone test (720p60 inside a 1080p50 camera raster):
  18/36 valid pairs at +34.9924 ms, zero video events during three seconds of
  deliberate picture loss and successful recovery at both rates. These are
  controlled fixtures, not physical display/camera accuracy validation.
- Existing live MXL and NMOS suites passed, including +80/0/+35/-35 ms,
  routing/unrouting/re-routing, stale signals and interlaced field structure.
- Isolated direct/SRT staging produced 20 new CRC-valid pairs each in ten
  seconds. Direct was -0.0076 ms throughout; SRT ranged -9.9905 to +10.8264 ms.
  The SRT path's roughly one-frame variation remains; geometry changes do not
  establish its removal. Both browser players delivered 425 video frames and
  16 independently decoded audio packets in 8.448 seconds.

The locally signed iPhone app is prepared without TestFlight. A new physical
optical/acoustic comparison remains for the maintainer's later manual test.
Use the isolated staging players documented in ICM, show the player fullscreen
on the Mac, and keep only that player's audio audible. Saved calibration and
results must remain intact across development installations.
