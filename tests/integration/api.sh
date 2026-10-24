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
curl -sf -X POST "http://127.0.0.1:$PORT/api/v1/generator/stop" | grep -q '"stopped":true'
curl -sf "http://127.0.0.1:$NPORT/x-nmos/node/v1.3/self" | grep -q '"description":"mxl-avsync"'
kill -TERM "$pid"
wait "$pid" || code=$?
test "${code:-0}" = 143
echo "api integration ok"
