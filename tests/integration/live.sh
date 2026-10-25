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
STALE_PID=""
cleanup() {
  [[ -n "$STALE_PID" ]] && { kill -TERM "$STALE_PID" 2>/dev/null || true; wait "$STALE_PID" 2>/dev/null || true; }
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

STALE="$WORK/stale-gen"
mkdir -p "$STALE"
OLD_ID=11111111-1111-4111-8111-111111111111
NEW_ID=22222222-2222-4222-8222-222222222222
printf '%s\n' "{\"id\":\"$OLD_ID\",\"label\":\"leftover\",\"description\":\"leftover\",\"tags\":{}}" > "$STALE/domain_def.json"
SPORT=18484
AVSYNC_ROLE=generator AVSYNC_EVENT_RATE=2 AVSYNC_STATE_DIR="$WORK/stale-state" NMOS_ENABLE=false \
  MXL_DOMAIN_SCAN_PATH="$WORK" MXL_OUTPUT_DOMAIN_DIR="$STALE" MXL_OUTPUT_DOMAIN_ID="$NEW_ID" \
  MXL_CLEANUP_ON_EXIT=true WEB_PORT="$SPORT" \
  "$BIN" serve >"$WORK/stale.log" 2>&1 &
STALE_PID=$!
stale_code=""
for _ in $(seq 1 50); do
  stale_code=$(curl -s -o /tmp/avsync-stale-livez.json -w '%{http_code}' "http://127.0.0.1:$SPORT/livez" || true)
  [[ "$stale_code" == "503" ]] && break
  sleep 0.1
done
test "$stale_code" = 503
grep -q '"status":"domain"' /tmp/avsync-stale-livez.json
detail=$(curl -sf "http://127.0.0.1:$SPORT/api/v1/status")
printf '%s\n' "$detail" | grep -q 'MXL_OUTPUT_DOMAIN_ID'
printf '%s\n' "$detail" | grep -q 'NMOS_SEED'
printf '%s\n' "$detail" | grep -q 'leftover directory'
printf '%s\n' "$detail" | grep -q "$OLD_ID"
test -f "$STALE/domain_def.json"
grep -q "$OLD_ID" "$STALE/domain_def.json"
kill -0 "$STALE_PID"
kill -TERM "$STALE_PID" 2>/dev/null || true
wait "$STALE_PID" 2>/dev/null || true
STALE_PID=""
test -f "$STALE/domain_def.json"
echo "leftover domain refuses a new id and fails livez"

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
python3 - "$AAPI" <<'PY'
import json, sys, urllib.request
api = sys.argv[1]
last = None
since = 0
while True:
    with urllib.request.urlopen(f"{api}/api/v1/events?limit=100&since={since}") as response:
        events = json.load(response).get("events", [])
    good = [e for e in events if e.get("video_content_vs_grain_ms") is not None and e.get("audio_content_vs_grain_ms") is not None and e.get("content_vs_grain_av_ms") is not None and e.get("grade") != "invalid"]
    if good:
        last = good[-1]
    if len(events) < 100:
        break
    since = int(events[-1]["seq"]) + 1
if last is None:
    sys.exit("closed loop event has no content-versus-grain fields")
for key in ("video_latency_ms", "audio_latency_ms", "video_index_shift_ms", "audio_index_shift_ms", "av_offset_arrival_ms",
            "video_content_vs_grain_vs_latency_ms", "content_vs_grain_av_vs_arrival_ms"):
    if key in last:
        sys.exit(f"{key} was reported without a locked clock")
print("content vs grain", last["seq"], last["video_content_vs_grain_ms"], last["audio_content_vs_grain_ms"], last["content_vs_grain_av_ms"])
PY
wait_jpeg() {
  local url="$1" out="$2" code=""
  for _ in $(seq 1 25); do
    code=$(curl -s -o "$out" -w '%{http_code}' "$url" || true)
    if [[ "$code" == "200" ]] && python3 -c 'import sys; b=open(sys.argv[1],"rb").read(2); sys.exit(0 if b==b"\xff\xd8" else 1)' "$out"; then
      echo "jpeg $(wc -c < "$out") bytes $url"
      return 0
    fi
    sleep 0.2
  done
  echo "jpeg missing $url (HTTP ${code})" >&2
  return 1
}
wait_jpeg "$GAPI/api/v1/preview/generated.jpg" "$WORK/generated.jpg"
code=$(curl -s -o "$WORK/no-input.json" -w '%{http_code}' "$GAPI/api/v1/preview/input.jpg" || true)
test "$code" = 404
grep -q 'no frame yet' "$WORK/no-input.json"
wait_jpeg "$AAPI/api/v1/preview/input.jpg" "$WORK/input.jpg"
wait_jpeg "$AAPI/api/v1/preview/generated.jpg" "$WORK/result.jpg"
curl -sf "$AAPI/api/v1/status" > "$WORK/astatus.json"
python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); assert d["result_flow"]; open(sys.argv[2],"w").write(d["result_domain"]+"\n"+d["result_flow"]+"\n")' "$WORK/astatus.json" "$WORK/result.txt"
RDOM=$(sed -n '1p' "$WORK/result.txt")
RFLOW=$(sed -n '2p' "$WORK/result.txt")
for _ in $(seq 1 15); do
  if "$SAMPLE" "$RDOM" "$RFLOW" | tee "$WORK/result-grain.txt" | grep -q 'width=960'; then
    break
  fi
  sleep 0.2
done
grep -q 'grain_rows=540' "$WORK/result-grain.txt"
grep -q 'valid=1' "$WORK/result-grain.txt"
cat "$WORK/result-grain.txt"
SEQ=$(newest_offset 0 | awk '{print $1}')
echo -n "live offset 80: "
curl -sf -X PATCH "$GAPI/api/v1/generator" -H 'Content-Type: application/json' -d '{"offset_ms":80}' \
  | python3 -c 'import json,sys; d=json.load(sys.stdin); assert d["ok"] and d["offset_ms"]==80'
curl -sf "$GAPI/api/v1/generator" | python3 -c 'import json,sys; assert json.load(sys.stdin)["offset_ms"]==80'
wait_offset 80 "$((SEQ + 1))"
SEQ=$(newest_offset 0 | awk '{print $1}')
echo -n "live offset 0: "
curl -sf -X PUT "$GAPI/api/v1/config" -H 'Content-Type: application/json' -d '{"AVSYNC_AV_OFFSET_MS":"0"}' \
  | python3 -c 'import json,sys; d=json.load(sys.stdin); assert d["ok"] and d["restart_required"]==[]'
curl -sf "$GAPI/api/v1/generator" | python3 -c 'import json,sys; assert json.load(sys.stdin)["offset_ms"]==0'
wait_offset 0 "$((SEQ + 1))"
SEQ=$(newest_offset 0 | awk '{print $1}')
echo -n "live offset 35: "
curl -sf -X PATCH "$GAPI/api/v1/generator" -H 'Content-Type: application/json' -d '{"offset_ms":35}' \
  | python3 -c 'import json,sys; d=json.load(sys.stdin); assert d["ok"] and d["offset_ms"]==35'
wait_offset 35 "$((SEQ + 1))"

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

echo "stale grade"
kill -TERM "$GEN_PID"
wait "$GEN_PID" || true
GEN_PID=""
sleep 7
python3 - "$AAPI" <<'PY'
import json, sys, urllib.request
api = sys.argv[1]
with urllib.request.urlopen(api + "/api/v1/measurement") as response:
    body = json.load(response)
if body.get("grade") != "invalid":
    sys.exit("grade stayed " + str(body.get("grade")))
age = float(body.get("age_ms", 0))
if age < 5000:
    sys.exit("age_ms " + str(age))
sentence = body.get("sentence") or ""
if "in sync" in sentence or "after video" in sentence or "before video" in sentence:
    sys.exit("stale sentence " + sentence)
metrics = urllib.request.urlopen(api + "/metrics").read().decode()
if 'mxl_avsync_grade{grade="pass"} 1' in metrics or 'mxl_avsync_grade{grade="invalid"} 1' not in metrics:
    sys.exit("grade metric still live")
print("invalid", sentence, "age_ms", round(age))
PY

echo "interlaced field grain"
if [[ -n "$GEN_PID" ]]; then kill -TERM "$GEN_PID"; wait "$GEN_PID" || true; GEN_PID=""; fi
if [[ -n "$ANA_PID" ]]; then kill -TERM "$ANA_PID"; wait "$ANA_PID" || true; ANA_PID=""; fi
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
