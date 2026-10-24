# Calibration

The phone measures only AV offset (`reference.type = none`). Its clock is not a latency reference.

`PhoneSession` in `avsync-core` locates the test picture with the fiducials, times the marker on the rolling-shutter row, detects the chirps, and subtracts a calibration offset. A record is valid for one model and OS version for 90 days. Otherwise the result is uncalibrated: the offset is shown and the grade colour is withheld.

The rig is an LED and a piezo on one microcontroller pin. Parts and firmware are in `mobile-app/calibration/`. The pulse has not been checked with an oscilloscope here. The Expo UI is in `mobile-app/`; capturing on a device needs Xcode or the Android SDK.
