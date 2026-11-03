# Phone app (iOS and Android)

One JavaScript UI and one C++ analyzer (`core/…/phone_analyzer.hpp`) for both
platforms. The native module only captures, reads QR codes and draws the
preview: [native contract](../docs/mobile-native.md).

## Run

Use Node/npm, Xcode and CocoaPods on macOS. From `mobile-app/`:

```sh
npm ci
npm test
npm run ios:device
npm start
```

Use the custom development build, not Expo Go. The local Avsync Expo module contains the camera view and C++ analysis bridge. After native changes run `pod install` in `ios/` and rebuild. On newer Xcode versions the SDK 52 CLI can hang on device discovery; a generic signed iOS build plus `xcrun devicectl device install app` / `device process launch` works. Unlock the iPhone to launch. Keep Mac and iPhone on the same network.

## Measure

Point the camera at an AVS3 test signal (QR and the two-field panel visible)
with its sound audible. After a few events the screen shows:

- the **AV offset**: positive means audio after video; calibrated when a
  calibration is saved for this phone and camera mode;
- the **spread** (±, half the 10–90 % range) and the number of events in the
  reading (the last 8 within 10 s);
- **video and audio latency** from the source time to this phone, once the
  clock is synchronized (NTP, automatic, renewed every 4 minutes).

When the signal drops, the last reading stays greyed for 8 s. **Save** stores
it in Results (tap a result to share it as JSON). **Restart** clears the
history, for example after moving the phone.

The camera runs at its fastest mode up to 240 fps. After 1.5 s it locks focus
and white balance and exposes for the full frame interval
([why](../docs/qr-flash.md)). A 30 fps camera needs a 1 event/s source.

## Calibration

Settings → Calibrate this phone. Point the camera at your hands, tap Start and
clap once. Then pick the first frame where the hands touch; the sound is marked
orange. Save stores sound time minus that frame's time; it applies to AV offset
only, for this OS version and camera mode, for 90 days.

## Signal

Signal → Start test signal shows the fullscreen AVS3 source with its sound
(2 events/s; 1/s under Options, with title, subtitle and image).

## Settings and Debug

Settings has calibration, clock and screen wake. Settings → Debug holds the
technical controls: camera rate (Auto, 240, 120, 60, 30), NTP server, clock
uncertainty limit, source TAI−UTC, live detector counters (bursts, crossings,
rejections, QR age, audio timecodes, frame gaps, exposure), the last events
per seq, share diagnostics, and clear results or calibration.

## Verification

- `npm test`: the view model, clock reference, calibration and playback logic.
- `npm run test:ios-native`: the real iOS bridge, CoreMedia buffers and Vision
  on a simulated 240 fps capture of the phone source (+20 ms AV, 30/50 ms
  latency; spread below 2 ms) and a calibration take.
- `npm run test:ios-generator`: the iOS generator picture, fields and audio.
- Android: see `modules/avsync/android` (in progress).

## EAS Cloud and TestFlight

The linked EAS project is `@leeo86/mxl-avsync`. Run EAS commands from
`mobile-app/`; the Git-root `.easignore` also includes the shared C++ core.

```sh
npx eas-cli@26.0.0 build --platform ios --profile production
npx eas-cli@26.0.0 submit --platform ios --profile production --latest
```

Production is a signed store/Release build with embedded JavaScript and the
native analyzer/generator. It launches without Metro, Expo Go or a running Mac.
Build numbers are managed remotely and incremented per production build.
The Xcode 26.2 cloud image meets Apple's iOS 26 SDK upload requirement.

EAS Submit uploads to App Store Connect/TestFlight; the public App Store
listing and review are separate. Signing credentials stay in EAS/Apple, not
in this repository. Inspect an upload without starting a build using
`eas build:inspect --platform ios --profile production --stage archive --output /tmp/avsync-eas-archive`.

App Store Connect record: `6827797077` (`io.leeo86.mxl.avsync`). The first
cloud delivery is version 2.0.0 / build 1; subsequent build numbers increment
in EAS. The internal TestFlight group is `Team (Expo)`. EAS Submit has a
remotely stored App Manager API key; no Apple password/key is committed here.
Expo Doctor passes 17/18 checks; the remaining check reports the existing
`expo-av` dependency as unmaintained. Production bundle export and clean
prebuild succeed; migration of calibration replay off expo-av is separate.

