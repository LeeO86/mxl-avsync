# Architecture

`mxl-avsync` is one binary with a dependency-free core.

- `avsync-core` plans events, synthesises and detects chirps, draws and reads the timing marker and Gray strip, pairs by event id, grades and computes statistics. It does not link MXL, NMOS, FFmpeg or a GUI.
- `mxl-avsync generate` / `analyze` / `validate` / `merge` are the file tools. FFmpeg libraries mux and demux. The default file is MOV + ProRes 422 HQ + PCM 24-bit at 48 kHz, which round-trips with container time bases 1/50 and 1/48000.
- `mxl-avsync serve` is the media function. `AVSYNC_ROLE` is `generator` or `analyzer`. It serves the web UI, REST, widgets, `/livez`, `/readyz` and `/metrics` on `WEB_PORT` (8180) and a minimal IS-04 / IS-05 node on `NMOS_PORT` (3332).

AV offset is `audio_time − video_time` on index time. A positive value means audio is late. Latency is reported only with a TAI/PTP reference; this build has no such reference, so latency fields stay absent and `uncertainty_status` for latency is `unknown`.

`/readyz` checks process/HTTP health and, when NMOS registration is configured, Query API visibility. The generator also waits for its video and audio flows to be created (so a generator without libmxl stays 503). An analyzer is ready before routing and remains ready through missing input or signal; `/statusz`, input-state metrics and the UI report media health separately. The control API, widgets and NMOS node continue to run without libmxl.
