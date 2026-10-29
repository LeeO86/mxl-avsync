# mxl-avsync 2.0.0 / AVsync iOS beta

Generator, analyzer and iPhone now share a QR + flash signal. A large QR carries
full event identity and send time; its adjacent field supplies the optical
onset. The default 1200-baud audio signal independently carries full timecode
with CRC/FEC. Select one or two events per second; 600-baud and legacy modes
remain available.

Upgrade compact-signal generators and receivers together. Older receivers do
not recognize AVS2. Existing legacy signals remain readable. AVS2 does not
provide the old strip's per-frame drop/duplicate diagnostics.

## Picture and controls

- Centered app icon with AVsync below it.
- Title and smaller Subtitle below the sync scale; existing captions persist.
- Black centerline on the moving box, below the white scale markers in both
  the iPhone's native layers and the container renderer.
- Cropping may discard outside picture elements while QR quiet zone and flash
  field remain visible. Late QR decoding does not move the optical timestamp.

## App and operation

Native iOS camera/microphone capture, calibration with frames after the sound,
measurement without calibration (zero correction), estimated per-channel
latency, independent signal pills, bounded outline tracking and saveable last
valid readings. The iPhone can also generate the shared signal with a stored
title/subtitle and privately imported image. TestFlight builds include their
JS/native code and do not require Expo Go, Metro or an available Mac.

The web interface preserves drafts during polling, supports source-picture
uploads, recovers unavailable previews and shows active generation in a green
status pill. NMOS/readiness/clock behavior from 1.0.1 is retained.

## Validation and limits

Core/clock/picture, file/codec, native Vision/AVFoundation, live MXL, NMOS and
browser paths are covered by the repository's regression suites. The QR/flash
suite includes crops, rotation, delayed QR delivery/pixel settling, blank and
frozen controls, and full-timecode pairing. See docs/qr-flash.md and the
implementation plan for measured conditions.

Phone UTC and scheduled source output are estimates, with unknown physical
clock/display/speaker/camera accuracy. No independent physical timing accuracy
is claimed. Frame-scale variation can remain in SRT/browser/display paths.
Android capture is planned; this release does not ship an Android app.
