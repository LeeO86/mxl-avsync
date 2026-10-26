#!/usr/bin/env bash
# SIGKILL the generator. Leftover flow files must become no_signal, not a 1 Hz resync.
set -euo pipefail
unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY ALL_PROXY all_proxy
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${BIN:-$ROOT/build/mxl-avsync}"
export LD_LIBRARY_PATH="${MXL_LIB_DIR:-/opt/mxl/lib}${VCPKG_LIB_DIR:+:$VCPKG_LIB_DIR}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
[[ -x "$BIN" ]] || { echo "missing $BIN" >&2; exit 1; }

WORK="$(mktemp -d /dev/shm/avs-dead-XXXXXX)"
PIDS=()
cleanup() {
  for pid in "${PIDS[@]}"; do
    kill -KILL "$pid" 2>/dev/null || true
  done
  for pid in "${PIDS[@]}"; do
    wait "$pid" 2>/dev/null || true
  done
  rm -rf "$WORK"
}
trap cleanup EXIT

GPORT=18990
APORT=18992
GAPI="http://127.0.0.1:$GPORT"
AAPI="http://127.0.0.1:$APORT"

start_gen() {
  AVSYNC_ROLE=generator AVSYNC_VIDEO_FORMAT=1080p50 AVSYNC_AV_OFFSET_MS=0 AVSYNC_EVENT_RATE=2 \
    AVSYNC_STATE_DIR="$WORK/gstate" NMOS_ENABLE=false NMOS_SEED=dead-gen \
    MXL_DOMAIN_SCAN_PATH="$WORK" MXL_OUTPUT_DOMAIN_DIR="$WORK/gen" MXL_CLEANUP_ON_EXIT=false \
    WEB_PORT="$GPORT" NMOS_PORT=19190 \
    "$BIN" serve >"$WORK/gen.log" 2>&1 &
  PIDS+=($!)
}

start_ana() {
  local port="$1" state="$2"
  AVSYNC_ROLE=analyzer AVSYNC_AV_OFFSET_MS=0 AVSYNC_EVENT_RATE=2 \
    AVSYNC_STATE_DIR="$state" NMOS_ENABLE=false NMOS_SEED=dead-ana \
    MXL_DOMAIN_SCAN_PATH="$WORK" MXL_OUTPUT_DOMAIN_DIR="$WORK/ana" MXL_CLEANUP_ON_EXIT=false \
    WEB_PORT="$port" NMOS_PORT="$((port + 200))" \
    "$BIN" serve >"$WORK/ana-$port.log" 2>&1 &
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

wait_ready() {
  local url="$1"
  for _ in $(seq 1 120); do
    local code
    code=$(curl -s -o /dev/null -w '%{http_code}' "$url" || true)
    [[ "$code" == "200" ]] && return 0
    sleep 0.1
  done
  echo "readyz stayed down for $url" >&2
  return 1
}

resyncs() {
  local port="$1"
  curl -sf "http://127.0.0.1:$port/metrics" | python3 -c 'import sys,re; text=sys.stdin.read(); m=re.search(r"mxl_avsync_input_resyncs_total\{essence=\"video\"\} ([0-9.]+)", text); print(m.group(1) if m else "missing")'
}

mkdir -p "$WORK/gstate" "$WORK/astate" "$WORK/late"
echo "no-signal.sh work $WORK"
start_gen
wait_http "$GAPI/livez"
wait_ready "$GAPI/readyz"
start_ana "$APORT" "$WORK/astate"
wait_http "$AAPI/livez"
wait_ready "$AAPI/readyz"
for _ in $(seq 1 100); do
  curl -sf "$AAPI/api/v1/status" | grep -q '"input_state":"running"' && break
  sleep 0.1
done
curl -sf "$AAPI/api/v1/status" | grep -q '"input_state":"running"'
echo "running before kill:"
curl -sf "$AAPI/api/v1/status"
echo

kill -KILL "${PIDS[0]}"
wait "${PIDS[0]}" 2>/dev/null || true
PIDS=("${PIDS[1]}")
sleep 3

echo "after kill:"
curl -sf "$AAPI/api/v1/status"
echo
python3 - "$AAPI" <<'PY'
import json, sys, urllib.request
api = sys.argv[1]
with urllib.request.urlopen(api + "/api/v1/status") as response:
    status = json.load(response)
if status.get("input_state") != "no_signal":
    sys.exit("input_state " + str(status.get("input_state")))
if status.get("media_ready") is not False:
    sys.exit("media_ready stayed true")
if "head" not in str(status.get("detail", "")):
    sys.exit("detail " + str(status.get("detail")))
print("status no_signal")
PY
set +e
ready_code=$(curl -s -o /dev/null -w '%{http_code}' "$AAPI/readyz" || true)
set -e
test "$ready_code" = "200"
curl -sf "$AAPI/metrics" | grep -q 'mxl_avsync_input_state{essence="video",state="no_signal"} 1'
curl -sf "$AAPI/metrics" | grep -q 'mxl_avsync_input_state{essence="video",state="running"} 0'
first=$(resyncs "$APORT")
sleep 3
second=$(resyncs "$APORT")
python3 -c 'import sys; a,b=float(sys.argv[1]),float(sys.argv[2]); sys.exit(0 if b-a <= 1 else 1)' "$first" "$second"
echo "resyncs $first then $second"

kill -KILL "${PIDS[0]}"
wait "${PIDS[0]}" 2>/dev/null || true
PIDS=()
start_ana 18994 "$WORK/late"
wait_http "http://127.0.0.1:18994/livez"
sleep 3
echo "late analyzer:"
curl -sf "http://127.0.0.1:18994/api/v1/status"
echo
python3 - "http://127.0.0.1:18994" <<'PY'
import json, sys, urllib.request
api = sys.argv[1]
with urllib.request.urlopen(api + "/api/v1/status") as response:
    status = json.load(response)
if status.get("input_state") != "no_signal":
    sys.exit("late input_state " + str(status.get("input_state")))
if status.get("media_ready") is not False:
    sys.exit("late media_ready stayed true")
print("late no_signal")
PY
curl -sf "http://127.0.0.1:18994/readyz" >/dev/null
echo "no-signal.sh ok"
