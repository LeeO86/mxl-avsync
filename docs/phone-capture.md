# Phone capture (mobile 2.1)

Both apps capture through the shared analyzer ([native contract](mobile-native.md)).

- **Camera:** back wide camera, the fastest mode up to 240 fps at 1280×720 or
  more (Debug can force 240, 120, 60 or 30 fps). After 1.5 s of automatic
  exposure, focus and white balance are locked and the exposure is set to the
  full frame interval, with the ISO that keeps the same brightness. Restart
  (or re-aiming after a rate change) repeats this.
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
last six audio bursts it shows whether the timecode decoded, how well the
start chirp matched (0–1), how many of the 56 sync bits were wrong (a packet
decodes with at most 6) and the level. On iOS it also shows the microphone
gain and whether iOS lets the app change it.
