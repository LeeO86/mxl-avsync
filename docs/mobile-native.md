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
| `generateStyled(rate, taiUtcSeconds, options)` | the same with string options; `mode: 'calibration'` shows the calibration pattern and resolves with `calibration` or `calibrationError`; `senderCalibrationMethod/OffsetMs/UncertaintyMs/Day/Route` put the sender calibration in the QR (iOS; [phone-generator.md](phone-generator.md#calibration-mode)) |

## Camera view `Avsync`

Props: `active` (bool), `captureFps` (0 = automatic best rate), `taiUtcOffset`
(seconds, default 37). Methods: `beginCalibration()`, `resumeAnalysis()`,
`focusAt(x, y)` (tap to focus; x and y are 0–1 across the view, see below),
`beginCalibrator(mode)` (iOS only so far; `camera` or `display`, see
[calibrator.md](calibrator.md#app-side-built)).
Event `onReport` with one of:

- **status** — `PhoneAnalyzer::poll()` parsed, plus `captureContext`
  `{platform, model, os, audioRoute, sampleRate, cameraMode}` (iOS adds
  `inputGain, inputGainSettable`; Android adds `capturePath` `normal` or
  `high_speed`, and `captureStats.gpuReadMaxMs` on the high-speed path), `configuredFps`
  and `clockMapping {monotonicMs, deviceUnixMs, clockId, timebase}` for the
  report's `timeMs`. `reading.offsetMs` has the sender calibration from the
  AVS4 QR subtracted; `reading.rawOffsetMs` is as measured and
  `reading.senderCalibrationMs` is present when the QR carries one. The
  phone's own analyser calibration is not in the report (the app subtracts it).
- **calibration** — `{frames:[{timeMs, uri}], spikeTimeMs, fps, uri (WAV),
  waveform (256 peaks), waveformStartMs, waveformBinDurationMs,
  waveformDurationMs, audioStartMs, captureContext}` from
  `PhoneAnalyzer::take_calibration()`; frames as JPEG files.
- **calibrator-progress** — `{mode, pairs, wanted, flashes, clicks, elapsedMs}`
  about once a second during a calibrator take.
- **calibrator** — `{mode, method, offsetMs, uncertaintyMs, spreadMs, pairs,
  rejected, directionMs, readoutMs, fps, captureContext}` once at its end, from
  `PhoneAnalyzer::poll_calibrator()`.
- **error** — `{message}`.

## Capture rules

- Back wide camera, the highest frame rate up to 240 fps at 1280×720 or more
  (1920×1080 preferred at 120 fps and below). 30 fps works only with a
  1 event/s source.
- After about 1.5 s of automatic exposure, lock focus and white balance and set
  a fixed exposure of the **full frame interval** with the ISO that keeps the
  same brightness. A short exposure makes each edge time depend on where the
  edge falls between exposures (simulation: about 6 ms instead of 1 ms spread).
- The 1.5 s automatic step runs on camera start, on `resumeAnalysis()`
  (Restart, metered at the centre again) and on `focusAt`. Frames and audio of
  that step are dropped and the analyzer starts fresh at the lock (also when
  the lock fails), so no reading uses them. On Android, `resumeAnalysis()` now
  re-runs the step too (before, it only reset the analyzer).
- **Tap to focus** (`focusAt`): the same 1.5 s automatic step, with focus and
  exposure metered on the tapped spot, then the same lock. Frames and audio
  are dropped from the tap until the lock, and the analyzer starts fresh at the
  lock, so no frame of the automatic step reaches a reading. iOS converts the
  point with the preview layer (`captureDevicePointConverted`, which knows the
  aspect fill and rotation) and sets `focusPointOfInterest` and
  `exposurePointOfInterest`. Android undoes the preview's aspect-fill crop and
  rotation, maps the point onto the active array (the stream is its centred
  crop) and sets a square `CONTROL_AF_REGIONS` / `CONTROL_AE_REGIONS` (1/8 of
  the short side) where the camera supports regions; the high-speed session
  keeps continuous-video AF, so one AF cancel trigger releases the old focus
  lock first. Restart meters at the centre again (iOS point 0.5/0.5, Android
  the camera's default regions).
- Pass each frame's exposure start (`pts_ms`), exposure and rolling-shutter
  readout (0 when unknown) on the same clock as the audio.
- The core needs only the luma plane. The flash detector reads 50 points (two
  5×5 grids placed by the QR), a calibration take keeps at most 640 px, so any
  size that still shows the QR panel works.
- Audio: mono float, 48 kHz preferred, timestamps on the camera clock.
- QR: about 10 reads per second on a background thread, with the four corners
  normalized to the camera image; pass the capture time of that frame.
- Every 100 ms: set the device-UTC offset (`unix ms − capture ms`), redraw the
  picture outline, and poll (calibration) or snapshot + poll on a worker (live).

## Android capture paths

- **Normal:** a `CameraCaptureSession` with the preview and a YUV `ImageReader`.
  Used whenever its AE fps ranges reach the rate (Auto: the highest up to 240).
  A phone that lists 90 or 120 fps there gets it here, at the size whose YUV and
  preview streams both keep up (1 % slack on the listed frame duration).
- **High speed:** used when the rate (or Auto) is above what the normal session
  offers and the camera lists a constrained high-speed mode with a fixed range
  (`120–120`, `240–240`) at 1280×720 or more. That session takes GPU surfaces
  only, so `HighSpeedCapture.kt` gives it one `SurfaceTexture`:
  - each frame is drawn into a small offscreen target, four luma pixels per
    RGBA pixel, and read with `glReadPixels`: 320 px wide live, 640 px during a
    calibration take, 960 px about 10 times a second for the QR reader;
  - the texture is sampled in buffer order, so the luma has the orientation of
    the normal path's Y plane and QR corners need no change;
  - the timestamp is `SurfaceTexture.getTimestamp()`, the sensor timestamp, and
    goes through the same camera-clock mapping as the normal path;
  - the preview is drawn from the same frames at about 30 fps (one camera
    output only: with two, the burst list gives one of them only 30 fps);
  - exposure: high-speed requests ignore manual exposure and focus, so after
    1.5 s AE and white balance are locked where they stand and focus is locked
    with one AF trigger. `exposureMode` is `ae-locked`; the exposure is often
    shorter than the frame interval in bright light.
- **Fallback:** if the GPU setup, the session or the burst fails, or no frame
  arrives within 3 s, the view restarts with the normal session (status
  "High-speed capture failed (…) · using normal capture") until the rate changes.
- **Exposure lock timing:** the lock waits for the first capture result and
  retries every 100 ms (slow cameras such as the emulator's start late). After
  3 s of retries it gives up: the camera stays automatic and the hold ends, so
  frames are measured anyway.
