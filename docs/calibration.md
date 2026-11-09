# Calibration

The phone measures only AV offset (`reference.type = none`). Its clock is not a latency reference.

`PhoneSession` in `avsync-core` locates the test picture with the fiducials, times the marker on the rolling-shutter row, detects the chirps, and subtracts a calibration offset. A record is valid for one model and OS version for 90 days. Otherwise the result is uncalibrated: the offset is shown and the grade colour is withheld.

The rig is an LED and a piezo on one microcontroller pin. Parts and firmware are in `mobile-app/calibration/`. The pulse has not been checked with an oscilloscope here. The Expo UI is in `mobile-app/`; capturing on a device needs Xcode or the Android SDK.

## With the calibrator

The calibrator is a small board with an LED, a buzzer and two light sensors
([signal contract](calibrator.md), [hardware](../hardware/calibrator/README.md)). The phone powers it over a USB-C cable.
In the app: **Settings → With calibrator** (iPhone and Android; the clap
calibration stays as it is). Three steps:

1. **Plug in** the calibrator.
2. **Camera and microphone.** Lay the calibrator LED-down on the main rear
   camera and the phone screen-down on a table. The calibrator flashes and
   clicks at the same moment, 23 times in about 13 s. The app records them and
   shows **Camera vs mic**, e.g. `+12.3 ms ± 0.4`. **Save** stores it as this
   phone's calibration, exactly like a clap calibration (same record, method
   `calibrator-pulse`, valid for this model, OS version and camera mode for
   90 days; the ± is the spread of the pulses).
3. **Screen and speaker.** The phone opens its generator in calibration mode:
   two halves at the bottom of the screen swap black and white every 500 ms,
   and the speaker beeps 100 ms after each swap. Turn the calibrator over and
   lay it on the red outline. It clicks when the screen swaps; the microphone
   hears the click and then the beep, so its own delay cancels. The app shows
   **Screen vs speaker**, e.g. `−38.0 ms ± 0.3` (positive: the sound comes
   out after the picture). **Save** stores it as this phone's **sender
   calibration**: from then on its test signal sends it in the AVS4 QR
   (method `transponder`, offset in 0.1 ms, uncertainty rounded up to 0.5 ms,
   the date), so an analyser can take the phone's own output offset into
   account. It is valid for this OS version for 90 days and is only sent on the
   audio output it was measured on (the built-in speaker).

Why through the generator: the calibration picture and sound use the same
screen layers, display clock, audio engine and speaker as the real test signal,
so the offset belongs to the real signal.

**Analysers subtract it.** When the QR carries a sender calibration, every
analyser (phone, container, file) takes it off the measured AV offset. The
result is the corrected value; the raw value and the calibration stay visible:

- phone: Measure and Results show "raw +50.5 ms · sender calibration +38.0 ms"
  under the result; the report `reading` has `offsetMs` (corrected),
  `rawOffsetMs` and `senderCalibrationMs`; saved results keep `rawOffsetMs` and
  `senderCalibrationMs`;
- container: `/api/v1/measurement` `offset_ms` (corrected), `raw_offset_ms`,
  `sender_calibration_ms`; each event has `raw_av_offset_ms` and
  `sender_calibration_ms`; web UI Input card ("AV sync raw");
- file analyzer: the same event fields, and in `summary`
  `sender_calibration_ms` and `median_raw_av_offset_ms`.

"Not calibrated" changes nothing. The subtraction is in core C++ (`Pairer`),
so iOS, Android, container and file do it the same way. On the phone two
calibrations are in play, each subtracted once: the sender calibration (the
other phone's screen vs speaker, from the QR) in core, and this phone's own
analyser calibration (camera vs microphone) in the app. Raw means neither is
subtracted. Debug → Clear calibration clears both on this phone (after a
confirm).

**A TV or monitor.** The container generator can send the same pattern
(`AVSYNC_PATTERN=calibration`, web UI → Signal → Picture). Lay the calibrator
on the centre line of the screen and choose **Measure a TV or monitor instead**
in step 3: the phone only listens and shows the screen-vs-speaker offset of
that chain. It is not saved.

The analysis is shared C++ (`core/include/avsync/calibrator.hpp`) with unit
tests on synthetic signals. Not yet tested with a real board: see the open
points in [calibrator.md](calibrator.md#open-points).
