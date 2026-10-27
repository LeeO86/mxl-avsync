#!/usr/bin/env bash
# A registry stand-in. The node must show up in the Query API before /readyz, and an IS-05 activation must count.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${BIN:-$ROOT/build/mxl-avsync}"
export LD_LIBRARY_PATH="${MXL_LIB_DIR:-/opt/mxl/lib}${VCPKG_LIB_DIR:+:$VCPKG_LIB_DIR}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
[[ -x "$BIN" ]] || { echo "missing $BIN" >&2; exit 1; }

WORK="$(mktemp -d /dev/shm/avs-reg-XXXXXX)"
GPID=""
APID=""
RPPID=""
cleanup() {
  [[ -n "$APID" ]] && { kill -TERM "$APID" 2>/dev/null || true; wait "$APID" 2>/dev/null || true; }
  [[ -n "$GPID" ]] && { kill -TERM "$GPID" 2>/dev/null || true; wait "$GPID" 2>/dev/null || true; }
  [[ -n "$RPPID" ]] && { kill -TERM "$RPPID" 2>/dev/null || true; wait "$RPPID" 2>/dev/null || true; }
  rm -rf "$WORK"
}
trap cleanup EXIT

REG=18710
QUERY=18711
NPORT=18732
GPORT=18780
mkdir -p "$WORK/g" "$WORK/gen"
python3 - "$REG" "$QUERY" "$WORK/hide" <<'PY' >"$WORK/registry.log" 2>&1 &
import os, sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from threading import Thread
reg_port, query_port, flag = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]

class Handler(BaseHTTPRequestHandler):
    def _send(self, code, body=b'{"id":"node"}\n'):
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", "0" if code == 204 else str(len(body)))
        self.end_headers()
        if code != 204 and body:
            self.wfile.write(body)
    def do_GET(self):
        if os.path.exists(flag):
            self._send(404, b"")
        else:
            self._send(200)
    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0") or 0)
        if length:
            self.rfile.read(length)
        self._send(200, b"{}\n")
    def do_DELETE(self):
        self._send(204, b"")
    def log_message(self, fmt, *args):
        sys.stderr.write("%s %s\n" % (self.command, self.path))

def serve(port):
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()

Thread(target=serve, args=(reg_port,), daemon=True).start()
serve(query_port)
PY
RPPID=$!
for _ in $(seq 1 30); do
  curl -sf "http://127.0.0.1:$QUERY/x-nmos/query/v1.3/nodes/x" >/dev/null && break
  sleep 0.1
done

AVSYNC_ROLE=generator AVSYNC_EVENT_RATE=2 AVSYNC_AV_OFFSET_MS=0 NMOS_SEED=reg-gen \
  NMOS_ENABLE=true NMOS_DNS_SD=false \
  NMOS_REGISTRY_ADDRESS=127.0.0.1 NMOS_REGISTRY_PORT=$REG \
  NMOS_QUERY_ADDRESS=127.0.0.1 NMOS_QUERY_PORT=$QUERY \
  NMOS_HOST_ADDRESS=127.0.0.1 NMOS_PORT=$NPORT WEB_PORT=$GPORT \
  AVSYNC_STATE_DIR="$WORK/g" MXL_DOMAIN_SCAN_PATH="$WORK" MXL_OUTPUT_DOMAIN_DIR="$WORK/gen" \
  "$BIN" serve >"$WORK/gen.log" 2>&1 &
GPID=$!

registered=0
for _ in $(seq 1 80); do
  if curl -sf "http://127.0.0.1:$GPORT/api/v1/status" | grep -q '"nmos_registered":true'; then
    registered=1
    break
  fi
  sleep 0.25
done
test "$registered" = 1
curl -sf "http://127.0.0.1:$GPORT/metrics" | grep -q 'mxl_avsync_nmos_registry_up 1'
for _ in $(seq 1 40); do
  code=$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$GPORT/readyz" || true)
  [[ "$code" == "200" ]] && break
  sleep 0.25
done
test "$code" = 200
curl -sf "http://127.0.0.1:$GPORT/" | grep -q 'not registered'
echo "registered and ready"

SENDER=$(python3 -c 'import json,sys,urllib.request; ss=json.load(urllib.request.urlopen(sys.argv[1])); print(next(s["id"] for s in ss if s.get("label")=="AVSync Test Video"))' "http://127.0.0.1:$NPORT/x-nmos/node/v1.3/senders")
BEFORE=$(python3 -c 'import sys,urllib.request; text=urllib.request.urlopen(sys.argv[1]).read().decode();
import re; print(re.search(r"mxl_avsync_nmos_activations_total ([0-9.]+)", text).group(1))' "http://127.0.0.1:$GPORT/metrics")
curl -sf -X PATCH "http://127.0.0.1:$NPORT/x-nmos/connection/v1.2/single/senders/$SENDER/staged" \
  -H 'Content-Type: application/json' \
  -d '{"master_enable":true,"activation":{"mode":"activate_immediate"}}' >/dev/null
rose=0
for _ in $(seq 1 40); do
  NOW=$(python3 -c 'import sys,urllib.request,re; text=urllib.request.urlopen(sys.argv[1]).read().decode(); print(re.search(r"mxl_avsync_nmos_activations_total ([0-9.]+)", text).group(1))' "http://127.0.0.1:$GPORT/metrics")
  python3 -c 'import sys; sys.exit(0 if float(sys.argv[1]) > float(sys.argv[2]) else 1)' "$NOW" "$BEFORE" && rose=1 && break
  sleep 0.1
done
test "$rose" = 1
echo "activations $BEFORE -> $NOW"

APORT=18782
mkdir -p "$WORK/a"
AVSYNC_ROLE=analyzer NMOS_SEED=reg-ana NMOS_ENABLE=true NMOS_DNS_SD=false \
  NMOS_REGISTRY_ADDRESS=127.0.0.1 NMOS_REGISTRY_PORT=$REG \
  NMOS_QUERY_ADDRESS=127.0.0.1 NMOS_QUERY_PORT=$QUERY \
  NMOS_HOST_ADDRESS=127.0.0.1 NMOS_PORT=18734 WEB_PORT=$APORT \
  AVSYNC_STATE_DIR="$WORK/a" MXL_DOMAIN_SCAN_PATH="$WORK" MXL_OUTPUT_DOMAIN_DIR="$WORK/ana" \
  "$BIN" serve >"$WORK/ana.log" 2>&1 &
APID=$!
for _ in $(seq 1 100); do
  curl -sf "http://127.0.0.1:$APORT/readyz" >/dev/null && break
  sleep 0.25
done
curl -sf "http://127.0.0.1:$APORT/readyz" >/dev/null
curl -sf "http://127.0.0.1:$APORT/statusz" | python3 -c '
import json,sys
s=json.load(sys.stdin)
assert s["media_ready"] is False and s["input_state"]=="not_routed", s
'
echo "registered analyzer ready without routes"

touch "$WORK/hide"
dropped=0
for _ in $(seq 1 30); do
  if curl -sf "http://127.0.0.1:$GPORT/api/v1/status" | grep -q '"nmos_registered":false'; then
    dropped=1
    break
  fi
  sleep 0.2
done
test "$dropped" = 1
code=$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$GPORT/readyz" || true)
test "$code" = "503"
curl -sf "http://127.0.0.1:$GPORT/metrics" | grep -q 'mxl_avsync_nmos_registry_up 0'
for _ in $(seq 1 40); do
  code=$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$APORT/readyz" || true)
  [[ "$code" == "503" ]] && break
  sleep 0.2
done
test "$code" = "503"
curl -sf "http://127.0.0.1:$APORT/api/v1/status" | grep -q '"nmos_registered":false'
echo "query miss is not ready for either role"
echo "nmos-registry.sh ok"
