# Mobile native contract (iOS and Android)

Both apps share one JavaScript UI and one C++ analyzer
(`core/include/avsync/phone_analyzer.hpp`). The native layer only captures,
reads QR codes and draws the camera preview. Everything that measures is in
the core, so both platforms give the same numbers.

## Module `Avsync`

| Function | Returns |
| --- | --- |
| `supportsLiveCapture()` | `true` when the camera view below exists |
| `supportsGenerator()` | `true` when `generate` exists |
| `loadState()` / `saveState(json)` | the app's saved JSON (string) |
| `deviceVersion()` | OS version string |
| `clockNow()` | `{monotonicMs, clockId, timebase}`; timebase `ios-host` or `android-elapsed-realtime` |
| `synchronizeClock(server)` | Promise of `{source:'ntp', server, referenceUnixMs, referenceMonotonicMs, uncertaintyMs, roundTripMs, offsetMs, clockId, timebase}` (SNTP, 8 samples, smallest round trip) |
| `setScreenAwake(on)` | keeps the display on |
| `generate(rate, taiUtcSeconds, options)` | Promise; shows the fullscreen AVS3 source until closed, resolves `{skippedDisplayFrames}` |

## Camera view `Avsync`

Props: `active` (bool), `captureFps` (0 = automatic best rate), `taiUtcOffset`
(seconds, default 37). Methods: `beginCalibration()`, `resumeAnalysis()`.
Event `onReport` with one of:

- **status** — `PhoneAnalyzer::poll()` parsed, plus `captureContext`
  `{platform, model, os, audioRoute, sampleRate, cameraMode}`, `configuredFps`
  and `clockMapping {monotonicMs, deviceUnixMs, clockId, timebase}` for the
  report's `timeMs`.
- **calibration** — `{frames:[{timeMs, uri}], spikeTimeMs, fps, uri (WAV),
  waveform (256 peaks), waveformStartMs, waveformBinDurationMs,
  waveformDurationMs, audioStartMs, captureContext}` from
  `PhoneAnalyzer::take_calibration()`; frames as JPEG files.
- **error** — `{message}`.

## Capture rules

- Back wide camera, the highest frame rate up to 240 fps at 1280×720 or more
  (1920×1080 preferred at 120 fps and below). 30 fps works only with a
  1 event/s source.
- After about 1.5 s of automatic exposure, lock focus and white balance and set
  a fixed exposure of the **full frame interval** with the ISO that keeps the
  same brightness. A short exposure makes each edge time depend on where the
  edge falls between exposures (simulation: about 6 ms instead of 1 ms spread).
- Pass each frame's exposure start (`pts_ms`), exposure and rolling-shutter
  readout (0 when unknown) on the same clock as the audio.
- Audio: mono float, 48 kHz preferred, timestamps on the camera clock.
- QR: about 10 reads per second on a background thread, with the four corners
  normalized to the camera image; pass the capture time of that frame.
- Every 100 ms: set the device-UTC offset (`unix ms − capture ms`), redraw the
  picture outline, and poll (calibration) or snapshot + poll on a worker (live).
