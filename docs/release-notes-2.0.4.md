# mxl-avsync 2.0.4 (unreleased)

The generator keeps 50 grains per second on a busy node, the analyzer no
longer reports events one frame off when it is briefly starved, and every event
is counted. The signal, ports, NMOS resources and dependency pins are unchanged
from 2.0.3.

## Fixes

- **Generator render cost.** The live AVS2 picture was drawn and packed in
  full on every grain: colour bars 3.3 ms, v210 pack 2.5 ms, QR encode 1.0 ms,
  icon 0.6 ms, about 8 ms per 1080p grain on an idle core. On a node with
  about 70 % CPU from other applications this exceeded the 20 ms grain period:
  the platform saw 73 % of grains committed after their timestamp and 20 %
  missed. Now bars, icon and titles are drawn once per settings change; each
  grain redraws and repacks only what changed (sync scale every grain, flash on
  its edges, QR and event line once per event, GIF frames when they change).
  The output is byte-identical to a full render (unit tested for 1080p50,
  1080i25, 720p50 and 1080p59.94, with title, subtitle, PNG and GIF changes).
  The v210 packer is also faster.
- **Analyzer starved for a moment.** It jumped to the newest grain whenever it
  was one grain behind. Under CPU load that skipped the grain with an event's
  first flash frame, and the event came out one frame off. It now reads every
  grain in order and catches up; it jumps only when more than half the flow
  history behind.
- **Counted events.** `mxl_avsync_events_total{result}` is a real counter. It
  counted the last 1000 retained events, so it stopped growing after about
  8 minutes; an event the analyzer could not pair was not counted at all when
  only its audio arrived. Such events are now `missing_video`.

## Numbers

Per grain, 1080p50 AVS2, idle lab core (`avsync-render-bench`):

| | full render + pack (2.0.3) | 2.0.4 |
|---|---|---|
| p50 | 7.9 ms | 0.84 ms |
| p99 | 10.8 ms | 3.7 ms (event grains) |
| 2160p50 p50 | 33.8 ms | 3.3 ms |

Live, generator and analyzer on two CPUs with two bursty busy loops per CPU
plus four memory-bandwidth threads on four neighbouring CPUs, 5 min:

| | 2.0.3 | 2.0.4 |
|---|---|---|
| missed video grains | 805 / min (27 %) | 0 |
| commit − `T_k` p50 / p99 | +37 / +118 ms | −16.8 / −7.2 ms |
| generator CPU | 0.48 core | 0.14 core |
| events one frame off | 10 of 352 | 0 of 607 |

## SRT path ("3 of 4 events vanish")

- An x264 (15 Mbit/s, veryfast, zerolatency, GOP 50) + AAC 128k round trip of
  a 60 s 1080p50 AVS2 file pairs 118 of 118 events, all `pass`. The QR, flash
  and audio timecode survive that path.
- Two counting effects explain a low "paired" number. `events_total` stopped at
  the 1000 retained events (992 paired in 36 min is that window). Events whose
  video was lost were also never counted.
- With a generator that misses grains (2.0.3 under load, 27 % missed), a 2.0.3
  analyzer showed 326 events for about 360 sent and counted nothing else. The
  2.0.4 analyzer showed 347 paired and 14 `missing_video`, 361 in total.
  `input_grains_skipped_total` matched the generator's missed grains exactly
  (391).

## New metrics

- `mxl_avsync_generator_render_seconds` histogram (buckets 1, 2, 5, 10, 20, 40,
  80, 160 ms, +Inf): render + pack time per video grain.
- `mxl_avsync_input_grains_skipped_total{essence="video"}`: input grains the
  analyzer did not analyse.
- `mxl_avsync_events_total{result}` gains `quality` and `other`.

## What changes for the platform

- No new env. Expect the generator well under its CPU request and video
  committed before `T_k` (status `emit_offsets.video_commit_vs_timestamp_ms`).
- `events_total` now grows for the life of the process. Dashboards that read
  it as "events in the window" must use `increase()`. `AVSyncNoEvents` works as
  written.
- A settings change (title, subtitle, source picture) costs one full render,
  about 60 ms at 1080p: one late grain, not a missed one.
