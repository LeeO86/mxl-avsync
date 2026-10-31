# mxl-avsync 2.0.5 (unreleased)

Latency is now reported as an absolute value (emission → arrival) that is
never negative, with its uncertainty and the clock named next to it. The
operator can enter the source's emit offsets, which makes absolute latency
work behind gateways, mixers and SRT. Alert rules and a per-release bench
script are added. The signal, ports, NMOS resources and dependency pins are
unchanged from 2.0.4.

## Absolute latency

- **Headline.** The latency card, the widget and `/api/v1/measurement` show
  `video_latency_abs_ms` / `audio_latency_abs_ms` as "v ± u ms", with the
  clock source, state and accuracy (`clock`). The value is the arrival time
  minus the content timestamp minus the source's emit offset: what the chain
  adds after the source.
- **Never negative.** A value below 0 but within its uncertainty is 0
  ("0 ± u"). Further below 0 there is no value, and the event gets the flag
  `emit_offset_mismatch`: the entered emit offset or the clock is wrong.
- **Uncertainty.** Video 1 ms, audio 1.5 ms, plus the clock accuracy when the
  clock reports one.
  With a kernel clock on a phc2sys host the accuracy is unknown; the value is
  then shown with "accuracy unknown (not in ±)".
- **Diagnostics.** "Arrival vs timestamp", the entered emit offsets and the
  raw (signed) chain latency are on a separate Diagnostics card and stay in
  the API. Existing fields keep their names and values.

## Exact audio emit offset

The 2.0.4 bench read an audio chain latency of −2.1 ms median (p5 −4.4,
p95 +4.8) on the direct link. Two causes, both fixed:

- **Generator.** Audio went out in 10 ms blocks, so an event's onset was
  committed anywhere within a block of the nominal −25 ms. The audio writer
  now ends the block before each event onset and commits the onset at exactly
  `−(AVSYNC_AUDIO_LEAD_MS + 5 ms)`. Measured: −24.2 ms (spread under 0.1 ms;
  about 0.8 ms for the write itself). `/api/v1/status` `emit_offsets` adds
  `audio_commit_vs_onset_ms` (p50, min, max of the last 100 events).
  `audio_ms` is that measured p50 once ten onsets are known, and
  `audio_nominal_ms` is the nominal value, which is also what the NMOS tag
  carries.
- **Analyzer.** Audio arrival was estimated from the main loop's read time,
  which reads audio only after video grains. A second reader now polls the
  audio flow head every 0.5 ms and stamps each sample with the commit it saw.
- **Lab, direct link:** audio chain median +1.09 ms (p5 0.87, p95 1.31) at
  offsets 0 and 35 ms with the nominal −25. With the measured value copied
  from the status it is about +0.3 ms. No `emit_offset_mismatch`. The audio
  uncertainty is now 1.5 ms (was 6).
- **Mixed versions:** a 2.0.4 generator still emits audio −20 to −30 ms early.
  A 2.0.5 analyzer can then flag `emit_offset_mismatch` for audio. Upgrade the
  generator together with the analyzers.

## Where the source emit offsets come from

Per essence, in this order:

1. **Entered** (new): live settings `AVSYNC_SOURCE_EMIT_VIDEO_MS` and
   `AVSYNC_SOURCE_EMIT_AUDIO_MS` (empty, or −1000 to 1000 ms), set by env, in
   Settings → Latency, or with `PUT /api/v1/config`. Copy them from the
   generator's `/api/v1/status` → `emit_offsets.video_ms` / `audio_ms`
   (−20 / −25 with the defaults). They win, and work on every path (direct,
   across nodes, through a mixer, over SRT), because they do not depend on the
   routed flow.
2. **NMOS tag** (as in 2.0.2): the tag `urn:x-mxl:tag:avsync-emit-offset-ms/v1.0`
   on the generator flow the analyzer reads directly, from the Query API. It
   gives nothing behind a gateway or mixer, whose output flow has no tag.
3. **None:** no absolute latency. The measurement says
   `latency_basis: "relative to the source timestamp"`, and only the AV offset
   is reported, as before.

`source_emit_offsets` names the source overall (`source`) and per essence
(`video_source`, `audio_source`: `entered`, `nmos-tag` or `none`). No new NMOS
tag is added.

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

- **Chart:** on the analyzers behind fabrics, the mixer and SRT, set
  `AVSYNC_SOURCE_EMIT_VIDEO_MS=-20` and `AVSYNC_SOURCE_EMIT_AUDIO_MS=-25` (or the
  generator's `/api/v1/status` values if its leads are changed). On the direct
  analyzer the existing `NMOS_QUERY_ADDRESS` / `NMOS_QUERY_PORT` keep giving the
  offsets from the tag; setting the two values there too is allowed and wins.
- Dashboards and alerts: take `deploy/prometheus/avsync-rules.yaml` into the
  platform rules. `events_total` is a counter since 2.0.4.
- Per release: `tools/bench_paths.py --minutes 30 --generator http://<avgen>:8180
  direct=http://<avana>:8180 fabrics=http://<avana-x>:8180 mixer=http://<avana-f>:8180
  srt=http://<avana-s>:8180 --json bench.json`.
