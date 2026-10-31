# Phone capture comparison (mobile 2.0.6+)

Settings → Camera frame rate selects 60, 120, 240 fps or Maximum. The initial
selection is 120 fps. Maximum retains the previous fastest-supported behavior.
The native selector prefers that fastest format at each capped rate when its
range permits it, then the nearest raster. Hardware below a cap uses its own
maximum. Read the actual camera mode; selecting 120 never guarantees 120.

The preview reports configured camera rate separately from the delivered rate
over the latest six seconds. This counts received camera frames, including
ones where no picture is recognized; it is not the QR decode rate. Analysis
uses capture presentation timestamps, not callback delivery time. Lowering
load may reduce burst losses, while coarser sampling increases quantization
(240: 4.17 ms, 120: 8.33 ms, 60: 16.67 ms per frame). A stable lower-rate result
would support a load issue but would not isolate every display/browser effect.

## Repeatable comparison

1. Keep the same player, full-screen picture, source rate, audio path, phone
   position, lighting and display refresh. Synchronize the phone clock.
2. Start at 120 fps. Wait for fresh paired events; capture at least 30 events,
   then save. Check the acquisition clock and live/held label. Saving a held
   reading does not acquire a new sample or apply a newer NTP reference.
3. Select 240 fps and repeat. Each change clears the current reading and
   detector state. Compare the saved actual mode, video/audio latencies and
   event-local gaps, rather than two snapshots of the same held event.
4. Optionally repeat at 60 fps, accepting its coarser resolution. A different
   capture mode makes the stored calibration inactive without deleting it.
   Compare raw/uncalibrated values consistently, or calibrate each mode.

## Diagnostic evidence

`captureStats` is retained in native diagnostic reports and saved readings:

- `deliveredFps`, `windowMs`, `maxFrameGapMs`, `p95FrameGapMs`: rolling six-second
  delivered-frame cadence. Counts restart with capture/rate changes.
- `dropCallbacks`/`dropReasons`: AVFoundation didDrop callbacks (`late`,
  `outOfBuffers`, `discontinuity`, `unknown`).
- `estimatedMissingFramesFromPts`: inferred from configured interval and PTS
  gaps. This overlaps callback evidence; never add the two counts.
- `videoProcessing*Ms`, `audioProcessing*Ms`, `pollProcessing*Ms`: p95/max of
  the last 128 durations per kind. Callback duration includes any poll work
  it triggers; these are nested observations, not additive CPU totals.
- `eventFrameGaps`: bracketing frame times/gap at retained video events within
  the window, to test whether a pulse coincided with a burst loss.

The app discards late video frames rather than building a delivery backlog.
Telemetry does not establish physical capture or display accuracy. NTP shifts
both channels' UTC mapping and does not remove display cadence/transport jitter.

## Automated checks and limits

Pure Swift tests cover format/rate selection, stable raster preference, hardware
fallback, a 41.7-ms simulated burst gap, event bracketing, distinct counters and
reset. A shared-detector test feeds actual QR modules plus 40-ms flash pixels
at 60/120/240 fps and 1/2 Hz using known QR geometry and a 3-ms sampling phase.
It requires identity and onset within one capture interval. This is not a test
of Vision recognition or of a physical phone camera. Existing native recording
tests cover the decoder separately; actual lower-rate stability needs the
controlled phone/player comparison above.

## Screen awake (mobile 2.0.7)

Settings → Keep screen awake defaults on for iOS. Live permitted capture and
all calibration steps, including frame review, prevent idle dimming/locking.
The generator holds its own request only while output runs. Pausing measurement,
stopping output, leaving capture or backgrounding releases the relevant request.
The original idle timer is restored after the final request. Disabling the
setting opts out for both modes; stored results/calibration are unaffected.
Measurement does not force maximum brightness or alter the system auto-lock
preference. A manual press of the lock button still locks the device.

## Nonblocking live DSP (mobile 2.0.8)

The capture queue buffers audio with deferred timecode scanning and preserves
original camera PTS. At most one independent PhoneSession snapshot is analyzed
on a worker at the existing one-second cadence. Deep copies own all mutable
buffers; decoded packet history is merged back on capture, with generation and
source checks. Reset/routing never reuses an old worker result. The original
12-second history remains bounded; synchronous core callers keep their default.
`analysisMode=async-snapshot-v1`, `analysisDelayMs` and snapshot processing costs
distinguish worker duration from capture callback blocking. Worker delay counts
toward displayed reading age. No numerical jitter suppression was added.

## Calibration review (mobile 2.0.8)

The waveform's left play/pause and the video overlay control the same recorded
audio/video preview. Blue is playback position; orange marks the detected sound.
Tap the waveform to seek, or hold a frame arrow to repeat one-frame steps with
acceleration. Stepping pauses playback; pause before saving the selected frame.
The preview uses original captured images/timestamps and the WAV time origin,
including nonzero audio/video start offsets. Live calibration exports a clipped
WAV and dense waveform for the retained pre/post-contact window. Save uses the
original selected frame timestamp, not the interpolated preview clock.
