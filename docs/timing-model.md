# Timing model

File time starts at 0 at the first sample and the first frame. The first event is at 1000 ms, then every period, and the last event is at least one period before the end. At 50p and 2 events/s the period is 25 frames (500 ms), so a 60 s file holds 118 events and a 3 s file holds 4.

At 59.94p the 2/s period is not an integer frame count, so the plan uses 30 frames (500.5 ms) and records that rate. Interlaced 1080i25 places events on first fields, so the period in grains is even (26 fields at 2/s).

Live event `k` is at grain `k × period`, counted from the SMPTE ST 2059 epoch with the same index-to-timestamp mapping as the MXL C API. The chirp centre is a pure function of the absolute sample index and may sit between samples, so the live rounding residual is 0. File mode rounds the centre to the nearest sample and stores `rounding_residual_us`.

Pairing uses the Gray-strip sequence and the chirp id (`seq mod 4`). A match is accepted inside ±2 periods. Beyond that the result is `out_of_range`, not a small wrong offset.

Grades apply to the offset when the expected offset is 0, and to the deviation otherwise. Boundaries belong to the better grade: −5 ms is pass, +60 ms is warn.
