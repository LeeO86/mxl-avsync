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

Point the camera at an AVS4 test signal (QR and the two-field panel visible)
with its sound audible. After a few events the screen shows:

- the **AV offset**: positive means audio after video; calibrated when a
  calibration is saved for this phone and camera mode;
- the **spread** (±, half the 10–90 % range) and the number of events in the
  reading (the last 8 within 10 s);
- **video and audio latency** from the source time to this phone, once the
  clock is synchronized (NTP, automatic, renewed every 4 minutes).

When the signal drops, the last reading stays greyed for 8 s. **Save** stores
it in Results. **Restart** clears the history, for example after moving the
phone; the camera then adjusts focus and exposure at the centre for 1.5 s and
locks again before measuring. When the sender you hear has a reference (see
Results), a line under the number shows the difference, e.g. "Δ +18.4 ms vs
Studio iPhone".

The camera runs at its fastest mode up to 240 fps. After 1.5 s it locks focus
and white balance and exposes for the full frame interval
([why](../docs/qr-flash.md)). A 30 fps camera needs a 1 event/s source.

**Tap the camera picture** to focus on that spot: a yellow square shows where,
the camera adjusts focus and exposure there for 1.5 s and locks again. The last
reading stays greyed meanwhile; then the measurement starts fresh. Taps do
nothing during calibration.

## Results

Saved results are grouped by sender (the sender ID in the test signal).
Results without a sender ID are under "Unknown sender". Tap a result to share
it as JSON. The newest 100 results are kept (references are always kept).

- **Nickname** (pencil): give a sender a name. The default is a short form of
  its ID, because the generator's title is only drawn in the picture and the
  analyser does not read it. An empty name goes back to the default.
- **Reference** (★): one result per sender. All other results of that sender
  show Δ AV offset, and Δ video/audio latency when both results have a latency
  (both had a clock reference). Raw values stay visible. Tap ★ again to clear it.
- **check**: shown next to the Δ when the camera mode or the calibration offset
  differs from the reference's. Results saved before this version have no
  camera mode, so only the calibration is compared for them.

## Calibration

Settings → Calibrate this phone. Point the camera at your hands, tap Start and
clap once. Then pick the first frame where the hands touch; the sound is marked
orange. Save stores sound time minus that frame's time; it applies to AV offset
only, for this OS version and camera mode, for 90 days.

Settings → With calibrator (iPhone): a wizard for the calibrator board. Step 2
measures camera vs microphone with the board's flashes and clicks and saves it
like the clap calibration. Step 3 measures this phone's screen vs speaker with
the generator's calibration pattern and saves it as the sender calibration,
which the test signal then sends in its QR. Settings shows it under "Sender
calibration"; Debug → Clear calibration clears both. Details:
[calibration](../docs/calibration.md#with-the-calibrator).

## Signal

Signal → Start test signal shows the fullscreen AVS4 source with its sound
(2 events/s; 1/s under Options, with title, subtitle and image).

## Settings and Debug

Settings has calibration, clock and screen wake. Settings → Debug holds the
technical controls: camera rate (Auto, 240, 120, 60, 30), NTP server, clock
uncertainty limit, source TAI−UTC, live detector counters (bursts, crossings,
rejections, QR age, audio timecodes, frame gaps, exposure), the last events
per seq, share diagnostics, and clear results or calibration.

## Verification

- `npm test`: the view model, clock reference, calibration (clap and calibrator), playback and saved-results logic.
- `npm run test:ios-native`: the real iOS bridge, CoreMedia buffers and Vision
  on a simulated 240 fps capture of the phone source (+20 ms AV, 30/50 ms
  latency; spread below 2 ms) and a calibration take.
- `npm run test:ios-generator`: the iOS generator picture, fields and audio.
- Android: see [Android module](#android-module).

## Android module

`modules/avsync/android` builds the core into `libavsync-jni` (CMake, C++20,
arm64-v8a and x86_64 only: the core uses `__int128`) and implements the
[native contract](../docs/mobile-native.md) with Camera2, AudioRecord and ML Kit
(bundled QR model). Capture uses the normal session (ImageReader) up to the
best rate it lists; when 120/240 fps exist only in the constrained high-speed
mode, `HighSpeedCapture.kt` reads the frames on the GPU instead, with a
fallback to the normal session ([capture paths](../docs/mobile-native.md#android-capture-paths)).
After 1.5 s of auto exposure the exposure is fixed at the full frame interval
(high-speed: AE locked). Camera timestamps must be on the microphone's
boot clock: REALTIME is used directly, an UNKNOWN source is matched against
elapsedRealtime/nanoTime on the first frame, else the view reports
"Camera clock unknown on this device". The clock check is SNTP, like iOS; the
GNSS code stays in the module but the app requests no location permission.

Needs Android Studio's SDK, NDK 26.1, CMake 3.22.1 and JDK 17. From `mobile-app/`:

```sh
npx expo prebuild -p android --clean   # android/ is generated, not committed
cd android
./gradlew assembleDebug :avsync:testDebugUnitTest
./gradlew :avsync:connectedDebugAndroidTest   # with an emulator or device
```

Unit tests cover the camera mode choice (normal and high-speed), the
high-speed texture crop, the fixed-exposure ISO, SNTP and the
calibration WAV/waveform. `src/androidTest` checks the JNI bridge (live
snapshot/poll and a clap calibration take).

Not yet verified: the app on the emulator with a camera preview, the
instrumented JNI test, and any real device (frame rate, exposure lock, the
audio/camera clock alignment and calibration).

## Android test APK

GitHub Actions builds a release APK (arm64-v8a) for side-loading:
`.github/workflows/android-apk.yaml`, on demand (Actions → Android APK → Run
workflow) and on pull requests that touch the app or the core. Download the
artifact `avsync-<version>-<commit>`, unzip it and install the `.apk` (allow
installs from unknown sources, or `adb install -r -g`). It is signed with the Expo
template's debug key, so a newer build installs over an older one; it is not for a
store.

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

