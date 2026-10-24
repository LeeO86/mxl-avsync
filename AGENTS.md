# Agent notes

- Read `SPECIFICATION.md` and `IMPLEMENTATION_PLAN.md` before changing behaviour. Record every deviation in the plan.
- C++20, CMake, Ninja. `avsync-core` links neither libmxl, nmos-cpp, FFmpeg nor a GUI. `mxl-avsync` links FFmpeg for the file tools.
- MXL pin is `218ddaa0a08c12ffe75fc475ae65aa3d9eef16d7` with `-DMXL_ENABLE_FABRICS_OFI=OFF`. One `MXL_REF` in `docker/Dockerfile` and `.github/workflows/ci.yaml`.
- Positive `av_offset_ms` means audio is late. Do not report a latency without a TAI/PTP reference.
- Config precedence is environment > `AVSYNC_CONFIG_FILE` (else `<AVSYNC_STATE_DIR>/config.json`) > defaults. Invalid config exits 78; a port failure exits 75; SIGTERM exits 143.
- Metrics prefix `mxl_avsync_`. Ports 8180 (web) and 3332 (NMOS).
- The picture says TEST SIGNAL. The process never sends IS-05 to another device.
- Text is an embedded 8x8 bitmap, not Blend2D. QR decode is quirc, not zxing-cpp. The NMOS node is in-tree IS-04/IS-05, not nmos-cpp. Those three deviations are recorded in the plan.
