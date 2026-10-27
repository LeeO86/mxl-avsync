# iPhone test-signal source

Use a native iOS development build, open **Generate**, choose **1 or 2 chirps/s**,
then **Start fullscreen signal**. Hold the phone sideways. The raster rotates
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
`clk: "phone-utc"` in the QR prevent a new analyzer from treating the phone as a
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
the source. Restart creates a fresh source ID and clock mapping. Bluetooth and
other external output paths have unverified delays; use the built-in speaker.

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
# macOS/Xcode, booted arm64 iOS simulator:
python3 mobile-app/tests/native/generator.py
```

Core tests verify matching source-clock mapping, QR/audio identity, blind
1/2-Hz decoding and explicit estimated latency with locked/unlocked/unknown
receiver clocks. Picture tests cover direct input, a moving scaled tile,
picture loss and reacquisition. The native test checks actual UIKit QR images,
every marker/strip phase at both rates and the actual audio payload/waveform.
Software tests do not prove physical emission/capture timing or a real call path.

Apple API references: [display targetTimestamp](https://developer.apple.com/documentation/quartzcore/cadisplaylink/targettimestamp),
[audio buffer scheduling](https://developer.apple.com/documentation/avfaudio/avaudioplayernode/schedulebuffer(_:at:options:completionhandler:)),
[node output presentation latency](https://developer.apple.com/documentation/avfaudio/avaudionode/outputpresentationlatency).
