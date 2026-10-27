#!/usr/bin/env bash
# REST, widget and exit-code checks that do not need libmxl.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${BIN:-$ROOT/build/mxl-avsync}"
PORT="${WEB_PORT:-18280}"
NPORT="${NMOS_PORT:-18281}"
set +e
AVSYNC_ROLE=generator AVSYNC_EVENT_RATE=9 AVSYNC_STATE_DIR=/tmp "$BIN" serve >/tmp/avsync-bad.txt 2>&1
code=$?
set -e
test "$code" = 78
echo "ok invalid config exits 78"

AVSYNC_ROLE=generator AVSYNC_STATE_DIR=/tmp/avsync-api AVSYNC_WEB_ROOT="$ROOT/web" \
  WEB_PORT="$PORT" NMOS_PORT="$NPORT" NMOS_HOST_ADDRESS=127.0.0.1 \
  "$BIN" serve >/tmp/avsync-api.log 2>&1 &
pid=$!
trap 'kill -TERM $pid 2>/dev/null || true; wait $pid 2>/dev/null || true' EXIT
for i in $(seq 1 50); do
  curl -sf "http://127.0.0.1:$PORT/livez" >/dev/null && break
  sleep 0.05
done
curl -sf "http://127.0.0.1:$PORT/api/v1/info" | grep -q '"role":"generator"'
code=$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/readyz")
test "$code" = 503
curl -s "http://127.0.0.1:$PORT/metrics" | grep -q 'mxl_avsync_generator_events_total 0'
curl -sf -D - "http://127.0.0.1:$PORT/widget/generator" -o /tmp/widget.html | grep -i 'content-security-policy' >/dev/null
if curl -sf -D - "http://127.0.0.1:$PORT/widget/generator" -o /dev/null | grep -i 'x-frame-options'; then
  echo "widget must not send X-Frame-Options" >&2
  exit 1
fi
code=$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/widget/missing")
test "$code" = 404
code=$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/widget/generator?nope=1")
test "$code" = 400
curl -sf "http://127.0.0.1:$PORT/widget/generator" | tee /tmp/widget-gen.html | grep -q 'TEST SIGNAL ACTIVE'
grep -q 'widget-ready' /tmp/widget-gen.html
grep -q 'widget-size' /tmp/widget-gen.html
grep -q 'Mute' /tmp/widget-gen.html
grep -q 'Stop' /tmp/widget-gen.html
grep -q 'offset ' /tmp/widget-gen.html
curl -sf "http://127.0.0.1:$PORT/widget/avsync" | tee /tmp/widget-av.html | grep -q 'widget-ready'
grep -q '/api/v1/measurement' /tmp/widget-av.html
grep -q '/api/v1/statistics' /tmp/widget-av.html
grep -q '120:80' /tmp/widget-av.html
curl -sf "http://127.0.0.1:$PORT/widget/avsync?latency=false&trend=0" | grep -q 'const latency=false'
curl -sf "http://127.0.0.1:$PORT/widget/avsync?latency=false&trend=0" | grep -q 'const trend=false'
curl -sf -X POST "http://127.0.0.1:$PORT/api/v1/generator/stop" | grep -q '"stopped":true'
curl -sf -X PATCH "http://127.0.0.1:$PORT/api/v1/generator" -H 'Content-Type: application/json' \
  -d '{"source":"STUDIO A","subtitle":"desk@studio.example"}' | grep -q '"subtitle":"desk@studio.example"'
curl -sf "http://127.0.0.1:$PORT/icon.png" | cmp -s - "$ROOT/web/icon.png"
curl -sf "http://127.0.0.1:$PORT/favicon-32.png" | cmp -s - "$ROOT/web/favicon-32.png"
curl -sf "http://127.0.0.1:$PORT/apple-touch-icon.png" | cmp -s - "$ROOT/web/apple-touch-icon.png"
curl -sf -X POST "http://127.0.0.1:$PORT/api/v1/generator/source-image" -H 'Content-Type: image/png' \
  --data-binary @"$ROOT/tests/fixtures/transparent.png" | grep -q '"frames":1'
curl -sf "http://127.0.0.1:$PORT/api/v1/generator/source-image" | cmp -s - "$ROOT/tests/fixtures/transparent.png"
code=$(curl -s -o /dev/null -w '%{http_code}' -X POST "http://127.0.0.1:$PORT/api/v1/generator/source-image" \
  -H 'Content-Type: text/plain' --data 'not an image')
test "$code" = 415
curl -sf "http://127.0.0.1:$NPORT/x-nmos/node/v1.3/self" | grep -q '"description":"mxl-avsync"'
kill -TERM "$pid"
wait "$pid" || code=$?
test "${code:-0}" = 143

# Readiness is a control-plane contract even in the build without libmxl.
work=$(mktemp -d)
trap 'kill -TERM $pid 2>/dev/null || true; wait $pid 2>/dev/null || true; rm -rf "$work"' EXIT
AVSYNC_ROLE=analyzer NMOS_ENABLE=false NMOS_REGISTRY_ADDRESS= \
  AVSYNC_STATE_DIR="$work/state" MXL_DOMAIN_SCAN_PATH="$work/empty" \
  MXL_OUTPUT_DOMAIN_DIR="$work/output" WEB_PORT="$PORT" \
  "$BIN" serve >"$work/analyzer.log" 2>&1 &
pid=$!
for _ in $(seq 1 50); do
  curl -sf "http://127.0.0.1:$PORT/readyz" >/dev/null && break
  sleep 0.1
done
curl -sf "http://127.0.0.1:$PORT/readyz" | python3 -c '
import json,sys
s=json.load(sys.stdin)
assert s["ready"] is True and s["role"]=="analyzer", s
'
curl -sf "http://127.0.0.1:$PORT/statusz" | python3 -c '
import json,sys
s=json.load(sys.stdin)
assert s["media_ready"] is False and s["input_state"] in ("waiting", "not_routed"), s
'
kill -TERM "$pid"
code=0
wait "$pid" || code=$?
test "$code" = 143
echo "api integration ok"
