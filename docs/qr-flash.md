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
