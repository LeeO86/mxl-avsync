# iPhone test-signal source

Use the current native iOS/TestFlight build, open **Generate**, choose **1 or 2 chirps/s**,
set **Title**, **Subtitle** and optionally **Choose image**, then **Start fullscreen signal**. Hold the phone sideways. The raster rotates
inside the fullscreen screen; Measure/Results keep their portrait layout.
Film the entire test picture, including all four corner fiducials, with an
external camera. Keep only that phone's speaker audible. Send the camera's
picture and audio through the desired gateway to an updated live analyzer.
Transport/routing remain the responsibility of the gateway/controller.

The source uses the same compact 1200-baud codec as the server. A 100-ms white
pulse begins at the planned chirp onset, once or twice per second. Each audio
packet independently carries the source identity and timestamp; an image or
manifest is not needed to decode the audio data. The picture carries the same
identity/time and a 60-Hz source-frame strip (`720p60`). Source timestamps describe
**scheduled output**, not an independent measurement of light or sound emission.

## What the numbers mean

- **AV offset** measures the complete chain: phone display/speaker, camera and
  microphone, encoder, transport and the input's timestamping. Positive = audio
  late. It does not isolate the camera's error from the source's own output bias.
- **Estimated latency** subtracts the scheduled source timestamp from reception.
  Source TAI is derived from phone UTC plus the configured TAI−UTC value (default
  37 s). Phone clock error and physical display/speaker output delay are unknown.
  The server preserves its own clock status and marks each phone result
  `latency_estimated: true`, `source_reference: phone-utc-scheduled-output`,
  `latency_uncertainty_status: unknown`. A locked receiver does not validate the
  source. Optical AV uncertainty is also unknown until measured on real hardware.
- The manual HTTPS clock check is a coarse sanity check. It does not discipline
  the clock or calibrate display/speaker delay. Capture calibration in Measure
  belongs to the phone's camera/microphone and is not applied to source playback.
- A separate independently timed source-output calibration is required to
  subtract phone output bias and isolate a downstream camera's contribution.

A CRC-protected `estimated-source` flag (0x08) in the audio header and
the phone-clock flag in the QR prevent a new analyzer from treating the phone as a
verified PTP generator. Old decoders reject these new audio flags; update both
ends. Normal server and FILE packets retain their original bytes/semantics.

## Native scheduling and lifecycle

A native CADisplayLink supplies the next presentation target. The first source
event is planned 1.5 s ahead on that cadence. Subsequent source frames/events
follow a 60-Hz logical host-clock timeline. AVAudioPlayerNode buffers are queued
2.5 s ahead on the same host clock, subtracting the node's reported downstream
output presentation latency. This is an OS timing estimate, not a microphone or
photodiode measurement. All timing and synthesis run outside JavaScript.

The screen reports skipped logical display frames and the reported audio lead.
It prevents automatic screen lock and raises brightness while active, restoring
both when stopped. Stop/Close cancel queued sound and remove the timing picture.
App inactivity, audio interruptions, output/configuration changes, a wall-clock
jump over 100 ms, missed scheduling deadlines and the 23-hour session limit stop
the source. Restart creates a fresh clock mapping. Bluetooth and other external
output paths have unverified delays; use the built-in speaker.

## Source ID

The source ID (the QR test id; the audio packet carries its CRC-32) stays the
same when you stop and start the generator again. It is `phone-` plus 12 hex
digits of a hash of a random install ID, the app version with its build
number (for example `2.0.8 (12)`) and the timing settings (now only the event
rate). So you get a new ID after a reinstall, a new app version or build (also a
new TestFlight build) or another event rate. Name, subtitle and image do not change
it. The install ID is random, not a hardware ID, and only lives in the app's
settings. The container generator uses the same rule with its NMOS seed in
place of the install ID (`AVSYNC_TEST_ID` in the specification, §10.2). Test
vector: install ID `00000000-0000-4000-8000-000000000000`, version `2.0.8`,
`rate=1` gives `phone-985274814721`.

## What the QR carries (AVS4)

The picture QR is the compact `AVS4:` text (specification, "QR + optical code").
Besides event and time it carries:

- the **test id** itself, so the analyzer shows `phone-985274814721` and not
  `source-<hex>`;
- the **Title** field, at most 24 bytes (about 24 Latin letters, fewer with
  umlauts or emoji), cut at a whole character. It is only a label: it does not
  change the ID;
- the **sender calibration** of the phone's own screen-vs-speaker offset
  (method, offset, uncertainty, date). The offset has the same sign as every
  AV offset here: sound minus picture, positive = sound later. This calibration
  is not built yet, so the phone sends "not calibrated" (method `none`).

The QR keeps its size (version 4, 33 × 33 modules); it now uses error
correction M instead of Q to make room. The phone analyzer report shows these
fields in `signal`: `testId`, `name` and `senderCalibration` (`method`, and when
calibrated `offsetMs`, `uncertaintyMs`, `date`). The container analyzer shows
them in `/api/v1/status` `signal` and in the web UI's Input card. Old AVS2/AVS3
phones and analyzers cannot read AVS4: update all of them together.

The live reader first tries the inexpensive full-raster marker path, then a
cached, validated fiducial pose. Searches are limited to a 960-pixel search raster;
final rectification and QR use original pixels. The canonical strip uses the
source QR's frame rate, while AV measurements retain received MXL timestamps.
A missing/invalid picture does not publish a stale video event. The analyzer
keeps decoding audio during picture loss and can reacquire the picture.

## Build and validation

Expo Go does not contain this native module. Rebuild/install the iOS development
client after updating this branch (`npm install`, `npx pod-install`, then
`npx expo run:ios` or the existing Xcode workspace/device workflow).

```sh
cmake --build build --target avsync-core-tests avsync-picture-tests
ctest --test-dir build --output-on-failure
node --test mobile-app/src/*.test.mjs tests/web/*.test.cjs
# Linux with MXL: synthetic 720p60 source in a different camera raster
cmake --build build --target mxl-avsync avsync-phone-fixture
python3 tests/integration/phone_generator.py --binary build/mxl-avsync
# macOS/Xcode, booted arm64 iOS simulator:
python3 mobile-app/tests/native/generator.py
```

Core tests verify matching source-clock mapping, QR/audio identity, blind
1/2-Hz decoding and explicit estimated latency with locked/unlocked/unknown
receiver clocks. Picture tests cover direct input, a moving scaled tile,
picture loss and reacquisition. The native test checks actual UIKit QR images,
every marker/strip phase at both rates and the actual audio payload/waveform.
Software tests do not prove physical emission/capture timing or a real call path.
Executed local/native and Linux fixture results are recorded in the
[test matrix](test-matrix.md#native-phone-source--optical-live-analyzer-2026-10-27).

The Linux live regression runs 1/2-Hz phone plans in a 1080p50 camera flow,
checks the planned +35-ms AV difference, and blanks the picture for three
seconds while compact audio continues. It requires estimated source/latency
labels and verifies that no stale video event survives loss, followed by
successful reacquisition. A received camera frame bounds timing resolution;
this is not a physical-camera accuracy test. The fixture target exists only
when MXL is found. The harness uses its own random domain/flow IDs, temporary
tmpfs media, free loopback test port and process cleanup; it does not use Docker
or existing NMOS routes. Pass `--out` with a fresh directory to retain logs and
JSON evidence. Set `MXL_LIB_DIR`/`VCPKG_LIB_DIR` if required by the local loader.

Apple API references: [display targetTimestamp](https://developer.apple.com/documentation/quartzcore/cadisplaylink/targettimestamp),
[audio buffer scheduling](https://developer.apple.com/documentation/avfaudio/avaudioplayernode/schedulebuffer(_:at:options:completionhandler:)),
[node output presentation latency](https://developer.apple.com/documentation/avfaudio/avaudionode/outputpresentationlatency).


## Shared pattern and source labels

The mobile source uses the server renderer for EG1 color bars, machine markers,
QR, source labels/logo and the early/zero/late sync scale. The square is red
while early, white at the event and green while late; its zero crossing shares
the planned chirp/flash onset. iOS animates it with native display-timestamped
layers over a static plate, with the same coordinates/colors as the renderer.
The bundled font is DejaVu Sans on both targets (CoreText on iOS, Blend2D on
the server); source-event labels describe scheduled phone output.

Name/subtitle and the selected image persist locally. Photos selection uses
the system picker, which shares only the image you select. The app stores a
normalized copy up to 512 px in private Application Support; Remove image
removes that copy, preserving the original in Photos. Images are not uploaded
with EAS builds or sent to a server. Labels/image are display identity and
do not change protected timecode/source IDs or calibration.
