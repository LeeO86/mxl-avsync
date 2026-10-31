# Phone time reference (mobile 2.0.5)

The native phone app incorporates the shared 2.0.5 detector/quality fixes.
This does not make a phone a PTP endpoint. AV offset uses the common capture
clock and does not need UTC. Latency additionally needs a source time and a
mapping from the capture clock to UTC.

## iOS: explicit SNTP exchange

Settings → Time reference selects an NTP hostname/IP (UDP port 123), default
`time.cloudflare.com`. Synchronize is explicit; there is no background polling.
The native client makes up to four requests, one second apart, on a worker.
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

## Android: native GNSS clock

Android 10+ and a device exposing raw GNSS clock measurements are required.
The user grants precise location and starts a check, normally outdoors. The
module requests GPS updates only during the check (maximum 25 seconds), never
reads/stores coordinates, and unregisters on success, failure, cancellation
or backgrounding. Unsupported receiver/disabled location/missing permission
is explicit; no fabricated GPS time or silent network fallback.

`GnssClock.timeNanos - fullBiasNanos - biasNanos` supplies GPS time. The
receiver's leap-second value converts GPS to UTC. Crucially, its
`elapsedRealtimeNanos` and uncertainty map that time to Android's monotonic
clock; callback delivery time is not used as the GNSS timestamp. Three
consistent samples are required. A hardware-clock discontinuity clears the
sample set. Bias and elapsedRealtime uncertainties are combined at three
reported standard deviations, plus a rounding allowance: a statistical
estimate, not a guaranteed maximum error. Actual receiver accuracy remains
to be field-tested.

This adds an Android **clock module and permission/UI path**, not the missing
camera/microphone NDK analyzer port or Android signal generator. Both return
unsupported rather than offering a nonfunctional capture screen.

## Applying and retaining the reference

References are process-local and tied to a clock ID/timebase; they are never
restored from disk. They are invalidated on backgrounding (iOS host time may
not advance across sleep) and an in-flight check cannot reinstate a pre-sleep
reference. They expire after five minutes. The uncertainty grows by
an explicit 50-ppm holdover assumption (not a device accuracy guarantee).
The default qualification threshold is 20 ms, configurable from 1–500 ms.
An expired, incompatible or over-limit reference is not used for correction.
The app keeps timestamp-relative estimates from device UTC visibly unvalidated.

The camera maps its session master clock to Core Media host time. Each report
provides the original device-UTC mapping, so JS applies the UTC correction
once, independently of AV offset/calibration. A wall-clock step cannot silently
shift a qualified monotonic reference. Every saved reading retains the exact
clock source, mapping, age, correction, uncertainty, RTT (NTP), server and raw
latency diagnostics **at acquisition**; saving/rechecking never updates an
old reading. A synchronized iOS generator uses the same mapping for scheduled
source timestamps and stops at expiry; physical speaker/display output stays
estimated and encoded as such.

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

The existing calibration UI is unchanged pending the maintainer's next design.
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
  raw diagnostics and immutable saved results; calibration context mismatch.
- `python3 mobile-app/tests/native/clock.py` (macOS): actual Swift packet codec
  and Network.framework UDP client against a fake server with known offset;
  rejection of unsynchronized, wrong-origin and kiss-of-death replies.
- `./gradlew :avsync:testDebugUnitTest` from generated Android project: GPS/UTC
  epoch and leap conversion, elapsedRealtime mapping and invalid/stale samples.
- Existing core and native iOS fixture suites verify AVS2, audio-only, latency,
  sparse picture tracking and capture calibration independently of time sync.

References: [NTPv4](https://www.rfc-editor.org/rfc/rfc5905),
[Android GnssClock](https://developer.android.com/reference/android/location/GnssClock),
[Core Media clock conversion](https://developer.apple.com/documentation/coremedia/cmsyncconverttime(_:from:to:)).
Real iPhone NTP accuracy, Android hardware GNSS and physical latency validation
remain separate device tests; a successful simulator/JVM test does not establish
their accuracy.
