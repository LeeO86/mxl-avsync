# mxl-avsync 2.0.6

Metrics and an alert for the absolute latency, an alert threshold that fits
busy nodes, and a more exact audio emit time. The signal, ports, NMOS
resources and dependency pins are unchanged from 2.0.5.

## New metrics (analyzer)

All of them are exported from the first scrape.

| Metric | Type | Meaning |
|---|---|---|
| `mxl_avsync_latency_abs_ms{essence}` | gauge | absolute latency (emission → arrival), median over the grade window |
| `mxl_avsync_latency_abs_uncertainty_ms{essence}` | gauge | its half-width, median over the same window |
| `mxl_avsync_latency_abs_seconds{essence}` | histogram | every event with a value since start; buckets 1, 2, 5, 10, 20, 50, 100, 200, 500 ms, 1, 2, 5 s, +Inf |
| `mxl_avsync_emit_offset_mismatch_total{essence}` | counter | events with a chain latency below 0 beyond its uncertainty, which get no absolute value |

- **Why gauges and a histogram.** The gauges use the median over the grade
  window (the last `AVSYNC_GRADE_EVENTS` events with a value, default 10), the
  same window as the grade. A single late event does not move them, and they
  read right on a dashboard. Without a value (no source emit offset, clock not
  locked) they are `NaN`, never a false 0.
- **The histogram** is cumulative. It is the one to use for percentiles and
  alerts over any time range:
  `histogram_quantile(0.99, rate(mxl_avsync_latency_abs_seconds_bucket[10m]))`.
  The buckets cover both a direct link (a few ms) and an SRT chain (about
  0.5 s).

## Changed AV offset metrics

- **`mxl_avsync_av_offset_ms`** is now the **median over the grade window**:
  the last `AVSYNC_GRADE_EVENTS` valid events (default 10), the same events
  the grade uses. Until 2.0.5 it was the **last event**, although the
  specification said median.
  - A single outlier event no longer shows in the gauge.
  - A step change shows after about half the window (5 events, 2.5 s at
    2 events/s).
  - Dashboards or alerts that rely on the gauge jumping with one event should
    use the event API or `av_offset_seconds` instead.
- **`mxl_avsync_av_offset_deviation_ms`** is the window's median deviation
  from the expected offset. It was always 0.
- **`mxl_avsync_av_offset_seconds`** now counts every paired event since
  start. Before, it counted the last 1000 retained events, so its `_count`
  and `_sum` stopped growing after about 8 minutes and `rate()` /
  `increase()` on it read 0 from then on. Queries with
  `histogram_quantile(…, rate(…_bucket[…]))` now work. Its `_count` equals
  `events_total{result="paired"}`.

## Alerts

- **`AVSyncGeneratorRenderSlow`** now fires when the render p99 is above 75 %
  of the new generator gauge `mxl_avsync_generator_render_budget_seconds`.
  - The budget is one grain period plus `AVSYNC_VIDEO_LEAD_MS`: a grain's
    render starts at the previous grain's commit and is late only after its
    own timestamp. That is 40 ms at 50p with the default lead, so the alert
    fires at 30 ms; at 59.94p it is 36.7 ms, alert at 27.5 ms.
  - The fixed 10 ms fired permanently on the platform node: p99 17.6 ms with 0
    late grains. A platform override to 18 ms can be removed.
- **`AVSyncEmitOffsetMismatch`** (new): `increase(mxl_avsync_emit_offset_mismatch_total[10m]) > 0`.
  The entered or tagged source emit offset, or the clock, is wrong.
- Both rules are unit-tested with promtool in CI. A p99 of 17.6 ms against the
  40 ms budget does not fire.

## Audio emit time

The audio onset block (the AVS2 packet waveform) is rendered 3 ms ahead and
committed on time:
- **Measured:** −24.94 ms with a spread of 0.02 ms, against the nominal
  −25 ms.
- **2.0.5:** the packet synthesis made it −24.2 to −22.5 ms.
- **Direct link** with the tag's −25 ms: audio chain about +0.2 to +0.4 ms.
