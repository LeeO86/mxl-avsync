#!/usr/bin/env bash
# Closed-loop MXL: generator and analyzer on a tmpfs domain. SPECIFICATION §14.2.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${BIN:-$ROOT/build/mxl-avsync}"
SAMPLE="${SAMPLE:-$ROOT/build/avsync-sample-flow}"
export LD_LIBRARY_PATH="${MXL_LIB_DIR:-/opt/mxl/lib}${VCPKG_LIB_DIR:+:$VCPKG_LIB_DIR}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

[[ -x "$BIN" ]] || { echo "missing $BIN" >&2; exit 1; }

WORK="$(mktemp -d /dev/shm/avs-XXXXXX)"
GEN_PID=""
ANA_PID=""
cleanup() {
  [[ -n "$GEN_PID" ]] && { kill -TERM "$GEN_PID" 2>/dev/null || true; wait "$GEN_PID" 2>/dev/null || true; }
  [[ -n "$ANA_PID" ]] && { kill -TERM "$ANA_PID" 2>/dev/null || true; wait "$ANA_PID" 2>/dev/null || true; }
  rm -rf "$WORK"
}
trap cleanup EXIT

GPORT=18480
APORT=18482
GAPI="http://127.0.0.1:$GPORT"
AAPI="http://127.0.0.1:$APORT"

start_gen() {
  local offset="$1" format="$2"
  AVSYNC_ROLE=generator AVSYNC_VIDEO_FORMAT="$format" AVSYNC_AV_OFFSET_MS="$offset" \
    AVSYNC_EVENT_RATE=2 AVSYNC_STATE_DIR="$WORK/gstate" NMOS_ENABLE=false NMOS_SEED=live-gen \
    MXL_DOMAIN_SCAN_PATH="$WORK" MXL_OUTPUT_DOMAIN_DIR="$WORK/gen" MXL_CLEANUP_ON_EXIT=false \
    WEB_PORT="$GPORT" \
    "$BIN" serve >"$WORK/gen.log" 2>&1 &
  GEN_PID=$!
}

start_ana() {
  local offset="$1"
  AVSYNC_ROLE=analyzer AVSYNC_AV_OFFSET_MS="$offset" AVSYNC_EVENT_RATE=2 \
    AVSYNC_STATE_DIR="$WORK/astate" NMOS_ENABLE=false NMOS_SEED=live-ana \
    MXL_DOMAIN_SCAN_PATH="$WORK" WEB_PORT="$APORT" \
    "$BIN" serve >"$WORK/ana.log" 2>&1 &
  ANA_PID=$!
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
  for _ in $(seq 1 100); do
    local code
    code=$(curl -s -o /dev/null -w '%{http_code}' "$url" || true)
    [[ "$code" == "200" ]] && return 0
    sleep 0.1
  done
  echo "readyz stayed down for $url" >&2
  return 1
}

newest_offset() {
  local since="${1:-0}"
  python3 - "$AAPI" "$since" <<'PY'
import json, sys, urllib.request
api, since = sys.argv[1], int(sys.argv[2])
last = None
while True:
    with urllib.request.urlopen(f"{api}/api/v1/events?limit=100&since={since}") as response:
        events = json.load(response).get("events", [])
    good = [e for e in events if e.get("av_offset_ms") is not None and e.get("grade") != "invalid"]
    if good:
        last = good[-1]
    if len(events) < 100:
        break
    since = int(events[-1]["seq"]) + 1
if last is None or int(last["seq"]) < int(sys.argv[2]):
    sys.exit(2)
print(last["seq"], last["av_offset_ms"], last["grade"])
PY
}

wait_offset() {
  local want="$1" min_seq="${2:-0}"
  local line seq offset grade
  for _ in $(seq 1 80); do
    if line=$(newest_offset "$min_seq" 2>/dev/null); then
      seq=${line%% *}
      rest=${line#* }
      offset=${rest%% *}
      grade=${rest##* }
      python3 -c 'import sys; seq,want,off=int(sys.argv[1]),float(sys.argv[2]),float(sys.argv[3]); sys.exit(0 if seq>=want and abs(off-float(sys.argv[4]))<=2.0 else 1)' \
        "$seq" "$min_seq" "$offset" "$want" && { echo "$line"; return 0; }
    fi
    sleep 0.25
  done
  echo "no measurement near $want ms" >&2
  echo "--- generator ---" >&2
  tail -30 "$WORK/gen.log" >&2 || true
  echo "--- analyzer ---" >&2
  tail -40 "$WORK/ana.log" >&2 || true
  curl -s "$AAPI/api/v1/status" >&2 || true
  echo >&2
  return 1
}

mkdir -p "$WORK/gstate" "$WORK/astate"
echo "live.sh work $WORK"

start_gen 35 1080p50
wait_http "$GAPI/livez"
wait_ready "$GAPI/readyz"
echo "generator ready"
curl -sf "$GAPI/api/v1/status"
echo
start_ana 0
wait_http "$AAPI/livez"
wait_ready "$AAPI/readyz"
echo "analyzer ready"
curl -sf "$AAPI/api/v1/status"
echo
echo -n "offset 35: "
wait_offset 35 0
SEQ=$(newest_offset 0 | awk '{print $1}')

echo -n "offset 0: "
kill -TERM "$GEN_PID"
wait "$GEN_PID" || true
GEN_PID=""
sleep 0.2
start_gen 0 1080p50
wait_ready "$GAPI/readyz"
wait_offset 0 "$((SEQ + 1))"
SEQ=$(newest_offset 0 | awk '{print $1}')

echo -n "offset -35 after reopen: "
kill -TERM "$GEN_PID"
wait "$GEN_PID" || true
GEN_PID=""
sleep 0.2
start_gen -35 1080p50
wait_ready "$GAPI/readyz"
wait_offset -35 "$((SEQ + 1))"

echo "interlaced field grain"
kill -TERM "$GEN_PID"; wait "$GEN_PID" || true; GEN_PID=""
kill -TERM "$ANA_PID"; wait "$ANA_PID" || true; ANA_PID=""
rm -rf "$WORK/gen"
start_gen 0 1080i25
wait_ready "$GAPI/readyz"
STATUS=$(curl -sf "$GAPI/api/v1/status")
echo "$STATUS"
python3 -c 'import json,sys; d=json.loads(sys.argv[1]); assert d["interlaced"] is True; assert d["grain_rows"]==540; assert d["height"]==1080' "$STATUS"
FLOW=$(python3 -c 'import json,sys; print(json.loads(sys.argv[1])["video_flow"])' "$STATUS")
ok=0
for _ in $(seq 1 10); do
  if "$SAMPLE" "$WORK/gen" "$FLOW"; then ok=1; break; fi
  sleep 0.2
done
[[ "$ok" == 1 ]]
echo "live.sh ok"
