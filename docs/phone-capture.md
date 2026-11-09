# Phone capture (mobile 2.1)

Both apps capture through the shared analyzer ([native contract](mobile-native.md)).

- **Camera:** back wide camera, the fastest mode up to 240 fps at 1280×720 or
  more (Debug can force 240, 120, 60 or 30 fps). After 1.5 s of automatic
  exposure, focus and white balance are locked and the exposure is set to the
  full frame interval, with the ISO that keeps the same brightness. Restart
  (or re-aiming after a rate change) repeats this, metered at the centre again.
  Frames from these 1.5 s are never measured: the measurement starts fresh
  after the lock (camera start, Restart and tap to focus alike).
- **Tap to focus:** a quick tap on the camera picture runs the same 1.5 s step
  again, but focus and exposure look at the tapped spot. A yellow square shows
  the spot and fades after the lock. Frames from those 1.5 s are not used: the
  last reading stays greyed and the measurement starts fresh after the lock.
  Taps are ignored during calibration, so the clap take is never cut.
- **Android 120/240 fps:** many phones offer these rates only in the camera's
  high-speed mode. The app then reads the frames on the GPU (small luma copies)
  and locks the automatic exposure instead of fixing it to the frame interval,
  because that mode does not allow manual exposure. If the mode fails, the app
  falls back to the normal camera at its best rate. Debug shows the path
  ("Capture path normal" or "high-speed") and the GPU read time
  ([details](mobile-native.md#android-capture-paths)).
- **Why a full-frame exposure:** each light edge then becomes a ramp across
  one frame, which the detector times exactly from the edge samples. With a
  short exposure the edge can fall between two exposures, and the time can only
  be guessed to within the gap ([simulation](qr-flash.md#simulation)).
- **Timestamps:** frames and audio use the same capture clock. Each frame
  passes its exposure start and length (and the rolling-shutter readout where
  the platform reports it).
- **Lower rates:** 120 and 60 fps reach about the same stability as 240 fps,
  because a reading averages 8 transitions per event over 8 events. 30 fps
  needs a 1 event/s source.
- **Load:** every frame is sampled at 50 points only (two 5×5 grids); the QR
  is read about 10 times per second on a worker; the audio scan runs once a
  second on a copy of the analyzer.

Debug shows the delivered frame rate, the largest frame gap, dropped frames,
the locked exposure and the detector counters (bursts, crossings, rejected
fits, bursts without a QR, bursts named by the event grid). For each of the
last six audio bursts it shows whether the timecode decoded ("echo filter"
when it decoded only through the equalizer, see audio-timecode.md), how well
the start chirp matched (0–1), how many of the 32 sync bits were wrong (a
packet decodes with at most 6) and the level. On iOS it also shows the microphone
gain and whether iOS lets the app change it.
