# mxl-avsync 2.0.5 (unreleased)

Latency is now reported as an absolute value (emission → arrival) that is
never negative, with its uncertainty and the clock named next to it. The
analyzer gets the source's emit offsets from the operator instead of a custom
NMOS tag. Alert rules and a per-release bench script are added. The signal,
ports and dependency pins are unchanged from 2.0.4.

## Absolute latency

- **Headline.** The latency card, the widget and `/api/v1/measurement` show
  `video_latency_abs_ms` / `audio_latency_abs_ms` as "v ± u ms", with the
  clock source, state and accuracy (`clock`). The value is the arrival time
  minus the content timestamp minus the source's emit offset: what the chain
  adds after the source.
- **Never negative.** A value below 0 but within its uncertainty is 0
  ("0 ± u"). Further below 0 there is no value, and the event gets the flag
  `emit_offset_mismatch`: the entered emit offset or the clock is wrong.
- **Uncertainty.** Video 1 ms, audio 6 ms (1 ms plus half the generator's
  10 ms audio block), plus the clock accuracy when the clock reports one.
  With a kernel clock on a phc2sys host the accuracy is unknown; the value is
  then shown with "accuracy unknown (not in ±)".
- **Diagnostics.** "Arrival vs timestamp", the entered emit offsets and the
  raw (signed) chain latency are on a separate Diagnostics card and stay in
  the API. Existing fields keep their names and values.

## Change: emit offsets are entered, not read from NMOS

Only standard NMOS is used. **Removed:** the flow tag
`urn:x-mxl:tag:avsync-emit-offset-ms/v1.0` (added in 2.0.2) and the analyzer's
Query API lookup of it.

- New live settings on the analyzer: `AVSYNC_SOURCE_EMIT_VIDEO_MS` and
  `AVSYNC_SOURCE_EMIT_AUDIO_MS` (empty, or −1000 to 1000). They can be set by
  env, in Settings → Latency, or with `PUT /api/v1/config`.
- Copy them from the generator's `/api/v1/status` → `emit_offsets.video_ms` /
  `audio_ms`. With the defaults these are −20 and −25.
- This works on every path (direct, across nodes, through a mixer, over SRT),
  because it does not depend on the routed flow. Behind a gateway the 2.0.2–2.0.4
  lookup found no tag.
- Without the two values there is no absolute latency. The measurement says
  `latency_basis: "relative to the source timestamp"`, and only the AV offset
  is graded, as always.
- API: the generator's `emit_offsets` no longer has `nmos_tag`. The analyzer's
  `source_emit_offsets.source` is `entered` or `none` (was `nmos-query`).

## Alerts and bench

- **Alert rules.** `deploy/prometheus/avsync-rules.yaml` holds
  `AVSyncMissingEvents` (more than 5 % of events in 10 min not paired),
  `AVSyncNoEvents`, `AVSyncInputGrainsSkipped`, `AVSyncFail` / `AVSyncWarn`,
  `AVSyncGeneratorMissedGrains`, `AVSyncGeneratorLateGrains` (more than 1 %)
  and `AVSyncGeneratorRenderSlow` (render p99 above 10 ms). They are checked
  and unit-tested with promtool in CI. `AVSyncNoEvents` never matched before
  because of a label mismatch; this is fixed.
- **Bench script.** `tools/bench_paths.py` samples each analyzer's event list,
  keeps every event once by `event_id`, and prints one table row per path:
  events, paired, losses by reason, AV sync median (p5…p95), warn/fail share,
  latency, correlation and audio uncertainty. It also prints generator
  missed/late grains and commit time when `--generator` is given. It uses only
  the Python standard library.

## What changes for the platform

- **Chart:** for absolute latency, set on each analyzer
  `AVSYNC_SOURCE_EMIT_VIDEO_MS=-20` and `AVSYNC_SOURCE_EMIT_AUDIO_MS=-25`, or the
  values from the generator's `/api/v1/status` if its leads are changed.
- `NMOS_QUERY_ADDRESS` / `NMOS_QUERY_PORT` are **no longer needed for chain
  latency**. Keep them only if `/readyz` should wait until the node is listed
  in the registry (that use is unchanged).
- Dashboards and alerts: take `deploy/prometheus/avsync-rules.yaml` into the
  platform rules. `events_total` is a counter since 2.0.4.
- Per release: `tools/bench_paths.py --minutes 30 --generator http://<avgen>:8180
  direct=http://<avana>:8180 fabrics=http://<avana-x>:8180 mixer=http://<avana-f>:8180
  srt=http://<avana-s>:8180 --json bench.json`.
