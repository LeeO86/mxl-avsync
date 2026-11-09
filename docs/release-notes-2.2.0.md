# mxl-avsync 2.2.0

Calibration with the calibrator board, an Android test signal, sender
calibration in a new picture QR (AVS4), and a test signal on the phone that no
longer stops after 5 minutes. Full list in [CHANGELOG](../CHANGELOG.md).

## Picture QR AVS4 (update everything together)

- AVS4 replaces AVS2/AVS3. Old phones and containers cannot read it, and 2.2.0
  cannot read the old QRs: update all containers, iPhones (TestFlight) and
  Android phones (APK) at the same time.
- It carries the real test ID, the generator name (Title, at most 24 bytes) and
  the sender calibration. Same QR size; error correction M instead of Q.
- Analysers (phone, container, file) subtract the sender calibration from the
  AV offset and keep the raw value next to it. Containers send "not
  calibrated", so container to container nothing changes.

## Calibrator board

- Settings → With calibrator (iPhone and Android): step 1 plug in, step 2
  camera vs microphone (flash + click on the rear camera), step 3 screen vs
  speaker (the generator's calibration pattern; saved as the phone's sender
  calibration and sent in the QR).
- A TV or monitor can be measured with the container's calibration pattern
  (`AVSYNC_PATTERN=calibration`).
- Board files, firmware and signal contract: `hardware/calibrator/`,
  [calibrator.md](calibrator.md). The first boards are ordered; nothing is
  tested with a real board yet.

## Phone app

- Android test signal, same as the iPhone (picture, options, QR, stable sender
  ID, "Stopped" screen, calibration mode). Not tested on a phone yet.
- The test signal no longer stops about 5 minutes after the last clock check:
  the app keeps checking the clock and the signal slews to each check (at most
  0.1 ms per second, never jumping). A signal started before the first check
  switches from device UTC to NTP UTC.
- Stable sender ID; Results grouped by sender with nicknames (default: the
  Title from the QR), a reference per sender and Δ values; tap to focus.
- Stop shows a large "Stopped"; Clear calibration asks first and clears both
  calibrations.
- Android: 120/240 fps via the high-speed camera mode, icons and status bar
  fixed, test APK from GitHub Actions.
- Shipped as TestFlight Mobile 2.2.0 and the Android APK from the v2.2.0 tag.

## What changes for the platform

- Not compatible with 2.1.x (AVS4). Generator and analyzer share one pin, so
  they move together.
- Default test ID: with `AVSYNC_TEST_ID` unset, the generator uses
  `<NMOS seed>-` plus a short hash of the seed, the app version and the timing
  settings. It stays the same over restarts but changes with each version. A
  set `AVSYNC_TEST_ID` is used as before.
- New setting `AVSYNC_PATTERN` (default `test`, live; `calibration` sends the
  calibrator pattern). Nothing else in the environment changes.
- Analyzer `/api/v1/status` has a new `signal` object; `/api/v1/measurement`
  and events gain `raw_offset_ms` / `raw_av_offset_ms` and
  `sender_calibration_ms` when the QR carries a calibration.
- No metric is new, renamed or removed. Ports, NMOS resources and labels are
  unchanged. The analyzer's source emit offsets (`AVSYNC_SOURCE_EMIT_*`) keep
  their meaning; they are separate from the sender calibration.
- In-place upgrade.
