# mxl-avsync 2.0.2

Fixes the generator's video timing under CPU load and makes the latency values
readable. The signal (AVS2), ports, metric names and dependency pins are
unchanged from 2.0.1. Existing API fields keep their names and values; new
fields are added.

## Fixes

- **Missed video grains under load.** The 2.0.1 generator rendered grain `k`
  after `T_k` and took the grain index from `mxlGetCurrentIndex`, which rounds
  to the nearest grain. A loop that woke more than half a grain late skipped a
  whole grain (`mxl_avsync_output_frames_missed_total{essence="video"}`), and a
  skipped event grain showed as one event one frame off (−20 ms at 50p,
  `video_content_vs_grain_ms` −20). Now grain `k` is rendered right after grain
  `k − 1` and committed at `T_k − AVSYNC_VIDEO_LEAD_MS` (new, default 20 ms,
  restart, 0–100). MXL indexes grains by timestamp, so readers read it at its
  own index. A late grain is still written; grains are skipped only when the
  writer is more than four grains behind.
- **Audio on its own thread.** Audio is committed every 10 ms block, between
  `AVSYNC_AUDIO_LEAD_MS` and that lead plus one block before its timestamp. A
  slow video render no longer shortens the audio lead.
- **Late counter.** `mxl_avsync_output_frames_late_total{essence="video"}` now
  counts grains committed after their own timestamp.

Lab, generator and analyzer sharing two CPUs with a bursty busy loop, 5 min at
1080p50, 2 events/s:

| | 2.0.1 | 2.0.2 |
|---|---|---|
| missed video grains per minute | 43.8 | 0 |
| events one frame off | 10 of 605 | 0 of 605 |
| video commit − `T_k`, median / p99 | +13.5 / +29.6 ms | −18.6 / +2.2 ms |

## Latency presentation

- **AV sync first.** The UI and `/api/v1/measurement` lead with the index-based
  offset, labelled "AV sync (what MXL consumers see)" (new `av_sync_ms`, the
  same value as `offset_ms`). This is the value that is graded.
- **Arrival vs timestamp.** `video_latency_ms` / `audio_latency_ms` are the
  arrival time minus the content timestamp (new aliases
  `video_arrival_vs_timestamp_ms` / `audio_arrival_vs_timestamp_ms`). Directly
  behind the generator they show the generator's own write timing, so both are
  negative: about −16 ms video and −23 ms audio with the defaults. The latency
  values differ while the AV sync is 0, because the essences are written at
  different times but read by index.
- **Emit offsets.** The generator publishes them in `/api/v1/status`
  (`emit_offsets`: video −20 ms, audio −25 ms with the defaults, plus the
  measured video commit time p50/p99/max) and as the NMOS flow tag
  `urn:x-mxl:tag:avsync-emit-offset-ms/v1.0` on its video and audio flows.
- **Chain latency.** An analyzer with a Query API (`NMOS_QUERY_ADDRESS`, or the
  registry address) reads that tag of the flows it reads and shows
  `video_chain_latency_ms` / `audio_chain_latency_ms` = arrival − timestamp −
  source emit offset: what the chain adds after the source. On a direct path
  it is a few ms (the analyzer's own read time; audio ±5 ms for the block).
  Behind a gateway the routed flow carries no tag, and chain latency stays
  absent. The AVS2 payload has no spare field, so the offsets are not in the
  signal.

## PTP status URL

`AVSYNC_PTP_STATUS_URL` pages may now also send `grandmaster`, `port_state`,
`ptp4l_offset_ns`, `phc2sys_offset_ns` and `path_delay_ns`. They fill
`gm_present`, `gm_identity` (hex digits only, for example `ec4670fffe00fe77`),
`port_state` and `master_offset_ns` (from `ptp4l_offset_ns`), and the status
`ptp` object shows the offsets and path delay. Before, the status said
`gm_present false` and `port_state unknown` next to `locked`. The minimal page
with only `sync_state` and `accuracy_ns` still works.

## What changes for the platform

- New env `AVSYNC_VIDEO_LEAD_MS` (default 20, restart). No change is needed.
- Expect on a direct generator → analyzer path: video latency about −16 ms
  (was about +18 ms), audio about −23 ms, `av_offset_arrival_ms` about −8 ms
  (was about −38 ms), missed video grains 0 at the load seen on the platform.
- For chain latency, give analyzers `NMOS_QUERY_ADDRESS` (or the registry
  address they already use for registration).
- Generator CPU is a little higher in the lab (0.59 vs 0.52 core under load;
  the render no longer waits).
