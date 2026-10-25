#!/usr/bin/env bash
# Every exported family is present at 0 before any measurement.
# video_latency_ms and audio_latency_ms are not exported: a 0 would be a false latency.
set -euo pipefail
unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY ALL_PROXY all_proxy
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${BIN:-$ROOT/build/mxl-avsync}"
PORT="${WEB_PORT:-18380}"
[[ -x "$BIN" ]] || { echo "missing $BIN" >&2; exit 1; }

AVSYNC_ROLE=generator AVSYNC_STATE_DIR=/tmp/avsync-metrics AVSYNC_WEB_ROOT="$ROOT/web" \
  WEB_PORT="$PORT" NMOS_PORT="$((PORT + 1))" NMOS_ENABLE=false \
  "$BIN" serve >/tmp/avsync-metrics.log 2>&1 &
pid=$!
trap 'kill -TERM $pid 2>/dev/null || true; wait $pid 2>/dev/null || true' EXIT
for _ in $(seq 1 50); do
  curl -sf "http://127.0.0.1:$PORT/livez" >/dev/null && break
  sleep 0.05
done
text="$(curl -sf "http://127.0.0.1:$PORT/metrics")"
require() {
  grep -q -F "$1" <<<"$text" || { echo "missing metric: $1" >&2; exit 1; }
}
require 'mxl_avsync_generator_active 0'
require 'mxl_avsync_generator_audio_muted 0'
require 'mxl_avsync_generator_events_total 0'
require 'mxl_avsync_output_frames_total{essence="video"} 0'
require 'mxl_avsync_output_frames_total{essence="audio"} 0'
require 'mxl_avsync_output_frames_late_total{essence="video"} 0'
require 'mxl_avsync_output_frames_late_total{essence="audio"} 0'
require 'mxl_avsync_output_frames_missed_total{essence="video"} 0'
require 'mxl_avsync_output_frames_missed_total{essence="audio"} 0'
require 'mxl_avsync_input_resyncs_total{essence="video"} 0'
require 'mxl_avsync_input_resyncs_total{essence="audio"} 0'
require 'mxl_avsync_events_total{result="paired"} 0'
require 'mxl_avsync_events_total{result="missing_audio"} 0'
require 'mxl_avsync_events_total{result="missing_video"} 0'
require 'mxl_avsync_events_total{result="duplicate"} 0'
require 'mxl_avsync_events_total{result="ambiguous"} 0'
require 'mxl_avsync_events_total{result="out_of_range"} 0'
require 'mxl_avsync_grade{grade="pass"} 0'
require 'mxl_avsync_grade{grade="warn"} 0'
require 'mxl_avsync_grade{grade="fail"} 0'
require 'mxl_avsync_grade{grade="invalid"} 0'
require 'mxl_avsync_av_offset_ms 0'
require 'mxl_avsync_av_offset_deviation_ms 0'
require 'mxl_avsync_av_offset_seconds_sum 0'
require 'mxl_avsync_av_offset_seconds_count 0'
require 'mxl_avsync_video_drops_total 0'
require 'mxl_avsync_video_duplicates_total 0'
require 'mxl_avsync_audio_correlation 0'
require 'mxl_avsync_audio_level_dbfs 0'
require 'mxl_avsync_reference_sync{type="tai-ptp",state="locked"} 0'
require 'mxl_avsync_reference_sync{type="tai-ptp",state="unlocked"} 0'
require 'mxl_avsync_reference_sync{type="tai-ptp",state="unknown"} 0'
require 'mxl_avsync_clock_synchronized '
require 'mxl_avsync_clock_esterror_seconds '
require 'mxl_avsync_clock_tai_offset_seconds '
require 'mxl_avsync_nmos_registry_up 0'
require 'mxl_avsync_nmos_activations_total 0'
if grep -q 'mxl_avsync_video_latency_ms\|mxl_avsync_audio_latency_ms' <<<"$text"; then
  echo "latency metrics must stay absent" >&2
  exit 1
fi
echo "$text"
echo "metrics.sh ok"
