#!/usr/bin/env bash
# PTP status document. No latency is claimed from index time.
set -euo pipefail
unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY ALL_PROXY all_proxy
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${BIN:-$ROOT/build/mxl-avsync}"
export LD_LIBRARY_PATH="${MXL_LIB_DIR:-/opt/mxl/lib}${VCPKG_LIB_DIR:+:$VCPKG_LIB_DIR}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

[[ -x "$BIN" ]] || { echo "missing $BIN" >&2; exit 1; }

WORK="$(mktemp -d /dev/shm/avs-ptp-XXXXXX)"
PIDS=()
cleanup() {
  for pid in "${PIDS[@]}"; do
    kill -TERM "$pid" 2>/dev/null || true
  done
  for pid in "${PIDS[@]}"; do
    wait "$pid" 2>/dev/null || true
  done
  rm -rf "$WORK"
}
trap cleanup EXIT

PTP_PORT=18790
GPORT=18784
APORT=18782
WPORT=18780
PTP_URL="http://127.0.0.1:${PTP_PORT}/ptp.json"

python3 - "$PTP_PORT" <<'PY' &
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
port = int(sys.argv[1])
class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith("/unlocked"):
            body = b'{"sync_state":"unlocked","accuracy_ns":"500"}'
        else:
            body = b'{"sync_state":"locked","accuracy_ns":"80"}'
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def log_message(self, fmt, *args):
        return
ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
PY
PIDS+=($!)

for _ in $(seq 1 50); do
  curl -sf "$PTP_URL" >/dev/null && break
  sleep 0.05
done
curl -sf "$PTP_URL" | grep -q '"sync_state":"locked"'

start_serve() {
  local role="$1" port="$2" url="$3" domain="$4"
  env AVSYNC_ROLE="$role" AVSYNC_STATE_DIR="$WORK/$port" NMOS_ENABLE=false \
    WEB_PORT="$port" NMOS_PORT="$((port + 200))" \
    MXL_DOMAIN_SCAN_PATH="$WORK" MXL_OUTPUT_DOMAIN_DIR="$domain" MXL_CLEANUP_ON_EXIT=false \
    AVSYNC_PTP_STATUS_URL="$url" AVSYNC_EVENT_RATE=2 AVSYNC_AV_OFFSET_MS=0 \
    "$BIN" serve >"$WORK/$port.log" 2>&1 &
  PIDS+=($!)
}

wait_http() {
  local url="$1"
  for _ in $(seq 1 80); do
    curl -sf "$url" >/dev/null && return 0
    sleep 0.1
  done
  echo "timed out waiting for $url" >&2
  return 1
}

wait_ptp() {
  local url="$1" expect="$2"
  for _ in $(seq 1 50); do
    if curl -sf "$url" | grep -q "$expect"; then
      curl -sf "$url"
      echo
      return 0
    fi
    sleep 0.1
  done
  echo "status did not match $expect" >&2
  curl -s "$url" >&2 || true
  echo >&2
  tail -20 "$WORK/"*.log >&2 || true
  return 1
}

mkdir -p "$WORK/gstate" "$WORK/astate" "$WORK/$WPORT" "$WORK/18781" "$WORK/18783"
echo "ptp.sh work $WORK"

start_serve generator "$GPORT" "" "$WORK/gen"
wait_http "http://127.0.0.1:$GPORT/livez"
start_serve analyzer "$APORT" "$PTP_URL" "$WORK/ana"
wait_http "http://127.0.0.1:$APORT/livez"
echo "locked:"
wait_ptp "http://127.0.0.1:$APORT/api/v1/status" '"sync_state":"locked"'
curl -sf "http://127.0.0.1:$APORT/api/v1/status" | grep -q '"accuracy_ns":"80"'
curl -sf "http://127.0.0.1:$APORT/metrics" | grep -q 'mxl_avsync_reference_sync{type="tai-ptp",state="locked"} 1'
curl -sf "http://127.0.0.1:$APORT/metrics" | grep -q 'mxl_avsync_reference_sync{type="tai-ptp",state="unknown"} 0'

gen_ready=0
ana_ready=0
for _ in $(seq 1 120); do
  code=$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$GPORT/readyz" || true)
  [[ "$code" == "200" ]] && gen_ready=1 && break
  sleep 0.1
done
for _ in $(seq 1 120); do
  code=$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$APORT/readyz" || true)
  [[ "$code" == "200" ]] && ana_ready=1 && break
  sleep 0.1
done
if [[ "$gen_ready" != 1 || "$ana_ready" != 1 ]]; then
  echo "flows did not open" >&2
  tail -30 "$WORK/$GPORT.log" >&2 || true
  tail -30 "$WORK/$APORT.log" >&2 || true
  exit 1
fi

echo "host:"
nproc
grep -m1 'model name' /proc/cpuinfo || true
echo "event:"
python3 - "http://127.0.0.1:$APORT" <<'PY'
import json, sys, time, urllib.request
api = sys.argv[1]
last = None
for _ in range(80):
    with urllib.request.urlopen(api + "/api/v1/events?limit=100") as response:
        events = json.load(response).get("events", [])
    need = ("video_latency_ms", "audio_latency_ms", "video_index_shift_ms", "audio_index_shift_ms", "av_offset_arrival_ms")
    good = [e for e in events if e.get("av_offset_ms") is not None and e.get("grade") != "invalid" and all(k in e for k in need)]
    if good:
        last = good[-1]
        break
    time.sleep(0.25)
if last is None:
    sys.exit(2)
ref = last["reference"]
if ref.get("sync_state") != "locked" or ref.get("accuracy_ns") != "80":
    print(json.dumps(last), file=sys.stderr)
    sys.exit(3)
for key in ("video_latency_ms", "audio_latency_ms", "video_index_shift_ms", "audio_index_shift_ms", "av_offset_arrival_ms"):
    if not isinstance(last.get(key), (int, float)):
        print(json.dumps(last), file=sys.stderr)
        sys.exit(4)
if abs(last["video_index_shift_ms"]) > 5:
    print("video index shift", last["video_index_shift_ms"], file=sys.stderr)
    sys.exit(5)
print(last["seq"], last["av_offset_ms"], last["grade"], ref["sync_state"], ref["accuracy_ns"],
      last["video_latency_ms"], last["audio_latency_ms"], last["video_index_shift_ms"],
      last["audio_index_shift_ms"], last["av_offset_arrival_ms"])
PY
clk=$(getconf CLK_TCK)
awk -v clk="$clk" '{printf "analyzer_utime_s %.3f\n", $14 / clk}' "/proc/${PIDS[2]}/stat"

# Stop the live pair before the short status cases, keeping the document server.
for pid in "${PIDS[@]:1}"; do
  kill -TERM "$pid" 2>/dev/null || true
done
for pid in "${PIDS[@]:1}"; do
  wait "$pid" 2>/dev/null || true
done
PIDS=("${PIDS[0]}")

echo "empty url:"
start_serve analyzer "$WPORT" "" "$WORK/empty"
wait_http "http://127.0.0.1:$WPORT/livez"
wait_ptp "http://127.0.0.1:$WPORT/api/v1/status" '"sync_state":"unknown"'

echo "dead url:"
start_serve analyzer 18781 "http://127.0.0.1:1/ptp.json" "$WORK/dead"
wait_http "http://127.0.0.1:18781/livez"
wait_ptp "http://127.0.0.1:18781/api/v1/status" '"sync_state":"unknown"'

echo "https refused:"
start_serve analyzer 18783 "https://127.0.0.1:${PTP_PORT}/ptp.json" "$WORK/https"
wait_http "http://127.0.0.1:18783/livez"
wait_ptp "http://127.0.0.1:18783/api/v1/status" '"sync_state":"unknown"'

echo "unlocked:"
start_serve analyzer 18785 "http://127.0.0.1:${PTP_PORT}/unlocked" "$WORK/unlocked"
wait_http "http://127.0.0.1:18785/livez"
wait_ptp "http://127.0.0.1:18785/api/v1/status" '"sync_state":"unlocked"'
curl -sf "http://127.0.0.1:18785/api/v1/status" | grep -q '"accuracy_ns":"500"'

echo "ptp.sh ok"
