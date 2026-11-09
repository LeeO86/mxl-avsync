# Phone time reference (mobile 2.1.0)

The native phone app incorporates the shared 2.0.5 detector/quality fixes.
This does not make a phone a PTP endpoint. AV offset uses the common capture
clock and does not need UTC. Latency additionally needs a source time and a
mapping from the capture clock to UTC.

## SNTP exchange (iOS and Android)

Settings → Debug → NTP server selects a hostname/IP (UDP port 123), default
`time.cloudflare.com`. The app checks the clock on its own when it starts or
comes to the front, and again every 4 minutes (a check is valid for 5 minutes).
When the holdover allowance (below) would push the uncertainty past the limit
within a minute, it checks again sooner, at most every 30 s (`clockCheckDue`
in `src/time-reference.mjs`). So a new check normally arrives before the old
one runs out, also while the test signal runs.
Settings → Sync clock now repeats it at once. The iOS client makes up to four
requests, one second apart, on a worker; Android (`Sntp.kt`) makes eight,
250 ms apart, with the same checks.
It uses RFC 5905's four timestamps and validates mode/version, synchronized
stratum, the echoed request timestamp, nonzero/ordered timestamps, clock
continuity and server root distance. Kiss-of-death stops further requests.
Timeout, malformed reply or contradictory samples give no reference.

The smallest estimated-uncertainty sample maps **Core Media host time** to
UTC. The reported clock contribution is half the network delay (covering
unknown path asymmetry), server root delay/dispersion/precision and local
stamping allowance. NTP is unauthenticated: these are estimates conditional
on the selected server's correctness, not independently certified bounds.
The system clock is never changed. LAN time servers may require iOS's local
network permission. The former HTTPS check is not used for correction.

## Android: no GPS time

Android uses NTP like iOS. The GNSS clock code (`GnssTime.kt`) stays in the
module for later, but the app requests no location permission and never uses
GPS time. Without a reachable NTP server (no network) Settings shows
"Not synchronized · no latency": the AV offset still works, the two latencies
are not shown.

## Applying and retaining the reference

References are process-local and tied to a clock ID/timebase; they are never
restored from disk. They are invalidated on backgrounding (iOS host time may
not advance across sleep) and an in-flight check cannot reinstate a pre-sleep
reference. They expire after five minutes. The uncertainty grows by
an explicit 50-ppm holdover assumption (not a device accuracy guarantee).
That is exactly 0.1 ms per two seconds: it increases the uncertainty allowance,
never the UTC correction or the measured latency. Settings shows only the
fixed **sync estimate** and the age of the check ("NTP ±1.2 ms · checked
42 s ago"). The growing **holdover allowance** is used internally against the
limit; Settings → Debug names it with the NTP-minus-device-UTC offset.
Repeat checks from the same server, process and timebase, one second to five
minutes apart, compare their UTC mappings at a common native timestamp. Their
difference and combined sample uncertainty are shown separately. Agreement
within that estimate cannot establish oscillator drift; a resolved difference
can also come from network asymmetry or the server. A new check resets the age
allowance because it supplies a new reference, not because the phone clock
has been corrected. Time-settings changes invalidate an in-flight old check.
The default qualification threshold is 20 ms, configurable from 1–500 ms.
An expired, incompatible or over-limit reference is not used for correction.
The app keeps timestamp-relative estimates from device UTC visibly unvalidated.

The camera maps its session master clock to Core Media host time. Each report
provides the original device-UTC mapping, so JS applies the UTC correction
once, independently of AV offset/calibration. A wall-clock step cannot silently
shift a qualified monotonic reference. Every saved reading retains the exact
clock source, mapping, age, correction, uncertainty, RTT (NTP), server and raw
latency diagnostics **at acquisition**; saving/rechecking never updates an
old reading. A synchronized generator (iOS and Android) uses the same mapping
for scheduled source timestamps; physical speaker/display output stays
estimated and encoded as such.

## Clock refresh while the test signal runs

The app keeps checking the clock while the test signal runs, by the same rule
as for measuring, and hands every new check to the running signal
(`updateGeneratorClock`, same JS call on iOS and Android). The signal then
moves to the new check gently instead of jumping (core
`PhoneGeneratorPlan::retime`, the same code on both phones):

- **What moves.** Only the source timestamps (the time in the QR and in the
  audio packet) of the events that are not queued yet. When the picture and
  the sound of an event play does not change: frames stay on the display's
  frame grid, and the sound and picture of one event always carry the same
  timestamp. Events already queued (up to 2.5 s ahead) keep theirs.
- **How fast.** At most 100 ppm: each event's timestamp moves by at most
  0.05 ms (2 events/s) or 0.1 ms (1 event/s) from the one before, that is
  0.1 ms per second. A typical difference of 1–5 ms between two checks is
  taken up in 10–50 s; the largest allowed (the limit, 20 ms by default) in
  200 s, before the next check is due. For an analyser this looks like a
  source clock 100 ppm off for that time: far below anything it reacts to, and
  the phone's own 50 ppm holdover allowance is of the same order. When the
  QR is missed and an analyser counts on from an older event, the error stays
  below 100 ppm × the gap (2 ms at the longest 20 s gap).
- **Too big a difference.** When a new check is further from the current
  timestamps than the uncertainty limit (default 20 ms), something is wrong
  with one of the two checks (network path, server). The signal does not
  slew then; it stops with "New clock check is … ms off (more than the … ms
  limit). Tap Start for a fresh clock mapping." Start uses the new check.
- **When it stops.** Only when no new check got through in time: the newest
  check is more than 5 minutes old, or its estimate plus the 50 ppm holdover
  passes the limit (no network, for example). Then it stops with "Clock
  reference expired. Close and synchronize again." as before.
- **What stays.** The test ID and source ID, the "Phone clock changed" check
  (it watches the phone's own wall clock against the host clock and does not
  use the check), and the other stop reasons.
- **Status line.** "NTP UTC" stays, now with the age of the newest check and,
  while the timestamps still slew, the part still to go: "NTP UTC (check 42 s
  ago, adjusting +0.8 ms)".
- **Started before the first check.** Pressing Start right after the app comes
  to the front, before its first check is done, starts the signal on device
  UTC. The first accepted check then switches it to NTP UTC with the same
  100 ppm slew, from the device-UTC timestamps to the NTP ones (same code as
  above, same on iOS and Android). The status line shows "NTP UTC (…,
  adjusting …)" from that moment. If the device clock is further from the
  check than the limit, the signal does **not** stop: it keeps running on
  device UTC and says so, e.g. "device UTC (clock check +35.0 ms off, more than
  the 20 ms limit)". A later check within the limit can still switch it. Once
  switched it follows the rules above (refresh, expiry, stop on a big jump).
- **What an analyser sees.** The QR and the audio packet carry no
  "device or NTP" mark: phone timestamps are always flagged as estimated, and
  the server reports `source_reference: phone-utc-scheduled-output` either way.
  So an analyser only sees the timestamps slew, never a jump or a flag change.
  The result when the screen closes has `clockApplied: true` and the check in
  `clockReference` as soon as the signal switched (at the switch, not when the
  slew ends).
- The calibration pattern uses no clock reference; it is not affected. The container generator uses the host/PTP clock and has no
  such refresh.

## Timestamp-relative versus emission-to-capture latency

Optional source emit offsets (video/audio, −1000…+1000 ms) can be copied from
the generator's `emit_offsets`. An empty value means unknown: the display
names the quantity **relative to source timestamp**. With entered offsets
and a qualified reference, estimated chain latency subtracts the offsets.
Values below zero within the **clock-only** contribution are shown as zero;
further below produce a mismatch, no absolute number, and preserve signed raw
values in Timing diagnostics. Entered offsets bind to the observed source;
a source change does not silently reuse another source's calibration.

The display always distinguishes the clock contribution from **unknown
capture and source timing uncertainty**. It does not reuse the container's
±1/1.5-ms arrival constants for a camera or microphone and never grades
absolute physical latency. Device UTC estimates and network/GNSS estimates
remain `estimated`, `accuracy: unvalidated`; AV offset remains independent.

## Capture calibration and device-to-device preparation

Mobile 2.0.8 calibration review supports synchronized audio/frame playback,
waveform seeking and accelerated held-arrow steps; see [capture](phone-capture.md).
New corrections carry model, OS, audio route types (not personal device names),
sample rate and configured camera mode. A mismatch disables the correction,
retaining the saved value; older corrections without a context also become
inactive when a current context is known. A route change resets capture to
avoid pairing across incompatible paths. Stored measurements are unchanged.

The 2.0.5 `two_way` core is compiled into iOS and the experimental native
`solveDeviceCalibration` bridge exposes its maths. It requires recorded
onsets/cross runs/loopbacks; it does not acquire two-device recordings or
provide a calibration wizard. Individual input/output latencies require a
trusted clock offset **and** one known latency. Reciprocal observations alone
cannot identify all of them. See [device-to-device](device-to-device.md).

## Checks

- `npm test` from `mobile-app`: clock/schema/expiry/correction/source-change,
  raw diagnostics and immutable saved results; calibration context mismatch;
  the check cadence, with a 15-minute test signal run that never expires while
  checks arrive and stops after the rule when they do not.
- Core `ctest` (`test_timecode.cpp`): the clock refresh slew (rate, refused
  big changes, queued events unchanged, same stamp for sound and picture) and
  the same 15-minute run on the generator plan.
- `python3 mobile-app/tests/native/clock.py` (macOS): actual Swift packet codec
  and Network.framework UDP client against a fake server with known offset;
  rejection of unsynchronized, wrong-origin and kiss-of-death replies.
- `./gradlew :avsync:testDebugUnitTest` from generated Android project: GPS/UTC
  epoch and leap conversion, elapsedRealtime mapping and invalid/stale samples.
- Existing core and native iOS fixture suites verify AVS3, audio-only, latency,
  sparse picture tracking and capture calibration independently of time sync.

References: [NTPv4](https://www.rfc-editor.org/rfc/rfc5905),
[Android GnssClock](https://developer.android.com/reference/android/location/GnssClock),
[Core Media clock conversion](https://developer.apple.com/documentation/coremedia/cmsyncconverttime(_:from:to:)).
Real iPhone NTP accuracy, Android hardware GNSS and physical latency validation
remain separate device tests; a successful simulator/JVM test does not establish
their accuracy.
