# Phone app

## Run

Use Node/npm, Xcode and CocoaPods on macOS. From `mobile-app/`:

```sh
npm ci
npm test
npm run ios:device
npm start
```

Use the custom development build, not Expo Go. The local Avsync Expo module contains the camera view and C++ analysis bridge. After native changes run `pod install` in `ios/` and rebuild. On newer Xcode versions the SDK 52 CLI can hang on device discovery; a generic signed iOS build plus `xcrun devicectl device install app` / `device process launch` works. Unlock the iPhone to launch. Keep Mac and iPhone on the same network.

## Live measurement

The Measure tab uses the original dark camera/guide/result layout. Native AVFoundation camera and microphone capture start together. The app selects the rear wide camera format with the highest supported maximum frame rate, choosing the larger raster on a tie, and explicitly sets both frame durations. The overlay distinguishes the configured camera rate from the rate actually processed. CPU load, dropped frames and light can affect achievable performance; the configured rate is not an accuracy claim.

The C++ phone detector receives camera luma and PCM audio on the capture session's shared timestamp timeline. Results update roughly once per second after the initial observation window; this is continuous analysis, not eight-second movie recording. Aim at the complete test picture with four visible corner markers and audible chirps (currently 2 events/second). Status text distinguishes missing picture, unreadable marker code and missing matching chirps. Save result stores a snapshot; Results lists saved measurements and tap-to-export JSON.

Calibration is optional. Missing, expired or incompatible calibration means **0 ms correction**, with an uncalibrated status. Positive offset means audio is late. A separate **estimated latency** panel uses a matching LIVE QR TAI reference and the phone clock; its clock/capture accuracy remains unknown, as described below.

## Calibration

Open Settings → Repeat calibration. Aim at a visible clap/tap or an LED/piezo reference. Start, wait briefly, then make one sharp sound. The same high-frame-rate camera/audio path captures the take. A ring buffer keeps 750 ms before the detected sound, then capture continues until at least 700 ms of BOTH video and audio have arrived after it. Frames on both sides are required. No fixed 100-frame cap truncates high-rate takes. If no transient is detected within 12 seconds, the UI asks to retry.

The review shows actual frames, signed time relative to the sound, counts before/after and frame-by-frame stepping. Replay plays the actual PCM sound. Saving stores sound PTS minus selected contact-frame PTS. The correction is applied once; it expires on OS change or after 90 days. Movie-path calibrations from earlier builds are intentionally incompatible with the raw native path.

## Detector and accuracy

Phone frames are rectified to a fixed 640×360 test-picture raster before decoding the Gray code; using arbitrary observed tile widths changes the integer strip geometry. The corner search uses local contrast and spatial geometry, avoids expanding fiducials into nearby text, and ranks candidates using the known reference strip. Cached geometry reduces per-frame work. The legacy file analyzer remains available for regression tests/debug recordings.

Manual clap calibration is approximate. Rolling shutter and independent LED/piezo validation remain open; observed event spread is not a validated capture uncertainty. The app reports uncertainty unknown, avoids validated grading and does not invent measurements without matched events. Android native capture/analysis is not implemented. Raw calibration frames/audio are temporary local cache files; they are not an archival media library.

## Verification

```sh
npm test
npm run test:ios-native
```

Native tests require Xcode, ffmpeg and a booted arm64 iOS simulator. They cover encoded +35 ms signal analysis, display orientation, missing audio/markers, timestamped live PCM16 processing, a calibration tail of at least 650 ms after the transient, and >100 selectable frames at 240 fps. These tests verify signal processing and buffering, not physical capture accuracy.

## Stability and diagnostics

The phone tracker confirms event codes across three frames, checks phase/sequence/timing consistency and uses the first confirming candidate frame's timestamp. Pose and sequence lock survive rolling audio/video pruning. Numeric live results require at least three consistent matched pairs. Excessive spread clears the reading instead of publishing an upper median from contradictory pairs; this can also withhold measurements from a genuinely unstable source path.

`Library/Caches/avsync-diagnostics.json` stores the latest 120 native reports across app restarts, including raw per-event IDs/timestamps/offsets and capture rates. It contains no live video/audio recording. This provides evidence for debugging remaining real-device instability; the capture accuracy remains unvalidated.

Diagnostic schema 2 adds current-interval picture/code counts, accepted video events, phase/sequence rejection counters, decoded/locked sequence identities, event ages, audio sample count/peak level, recognized chirps and correlations, and a `stage` explaining where analysis stops. Chirps are detected independently even when no video event was accepted. The UI uses current picture detection rather than lifetime counts, so a lost picture cannot remain reported as “listening for chirps.” Audio/chirp statistics cover the buffered window (up to six seconds), not just the latest report interval. Native regressions cover picture loss after prior successful detection, chirps without a picture, and valid video with silent audio. These diagnostics require a native rebuild; Metro reload alone cannot update them.


## Live recognition feedback and estimated latency

Independent audio and picture pills use gray for absent/stale input, orange for detection and green for a confirmed picture code or fully validated audio timecode. The chirp carries one of four IDs, **not a full timecode**: audio stays orange for the legacy four-ID signal, even after image pairing. Green is reserved for an independently decoded, CRC-validated audio timecode; an experimental codec is tested under `tests/audio-timecode/`, but it is not wired into the production generator or this app yet. Historical hits outside the current interval, ambiguous pairs and paused/stalled capture cannot keep a green pill. A native quadrilateral follows the actual detected corners at up to 10 Hz, matches portrait resizeAspectFill cropping, and expires if frames stop. It is orange when a picture is found and green when its code is confirmed; it is not the old fixed guide rectangle.

The phone decodes QR payloads with Apple's built-in Vision framework, at most once per second on a separate worker. A LIVE QR supplies the source event identity, TAI send timestamp, format/rate and configured audio offset. The event's camera/microphone PTS is mapped through the capture session clock to device UTC. TAI−UTC defaults to **37 seconds** for the current source and can be changed explicitly in Settings; this is a configured assumption, not discovered PTP lock. Nearby event projection uses the actual planned grain period, respects the 24-bit wrap and expires after three seconds. FILE signals, unknown source identity, expired QR and unavailable capture-clock mapping produce no absolute estimate. Sweep mode has no known per-event source audio offset, so only video latency is estimated there.

Video and audio estimates include device capture delay and may be negative if clocks are wrong. They are separate from the AV offset, are always labelled estimated, and do not acquire the AV calibration's correction or a verified uncertainty. Source clock accuracy is also unknown to the phone. The user explicitly requested useful estimates despite these limits; they do not replace the independent calibration/clock validation required by the original specification.

“Check clock” makes three HTTPS requests only when tapped, using the WorldTimeAPI-compatible service `https://worldtime.timezone.io/api/timezone/Etc/UTC` ([provider documentation](https://www.timezone.io/docs/worldtimeapi)). It brackets API−phone clock difference over request/response times without assuming symmetric network delay, reports RTT, rejects cached/invalid/inconsistent answers and local clock jumps, and expires after five minutes or a clock change. It does not set the phone clock or silently correct latency. API clock error and capture error are not bounded by this bracket; it is a **coarse sanity check** only. The original worldtimeapi.org endpoint was unreachable during development; the configured compatible endpoint returned valid UTC data. [NIST explains network-path accuracy limits](https://www.nist.gov/pml/time-and-frequency-division/time-distribution/internet-time-service-its).

Regression coverage includes pill progression and expiry, ambiguous/mismatched chirp identities, negative estimates, UTC/TAI conversion, source audio offsets, sequence wrap, source expiry, unsupported/file references, real native +120 ms video/+155 ms audio estimates, and four-corner aspect-fill mapping. A physical phone/Safari test remains necessary to validate overlay alignment, QR availability and performance on the capture path.


The integrated compact audio timecode now drives the green audio pill only
after CRC validation. Both 1/2-Hz sources are acquired from QR metadata; the
stability window retains at least three recent events at 1 Hz. Independently
decoded LIVE audio supplies estimated audio latency even without image/QR,
with device/source clock and capture accuracy still unknown. A native rebuild
is required (Expo Go cannot load this codec). See ../docs/audio-timecode.md.

## Generate on the iPhone

The **Generate** tab opens a native fullscreen source at 1200 baud, selectable
1/2 chirps/s. Film it with an external camera feeding the updated server
analyzer. AV sync includes the phone output; latency is explicitly estimated.
This requires a rebuilt native development client. See [workflow, timing and
validation limits](../docs/phone-generator.md). Run
`python3 tests/native/generator.py` from this directory with a booted simulator.
