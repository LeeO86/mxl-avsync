#!/usr/bin/env bash
# Generator emit offsets, analyzer chain latency from Query API flow tags, and the extended PTP status URL.
# A Python stand-in serves the Query API flows (with the generator's tags) and the PTP status page.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${BIN:-$ROOT/build/mxl-avsync}"
export LD_LIBRARY_PATH="${MXL_LIB_DIR:-/opt/mxl/lib}${VCPKG_LIB_DIR:+:$VCPKG_LIB_DIR}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
[[ -x "$BIN" ]] || { echo "missing $BIN" >&2; exit 1; }

WORK="$(mktemp -d /dev/shm/avs-chain-XXXXXX)"
GEN_PID=""
ANA_PID=""
STUB_PID=""
cleanup() {
  for pid in "$ANA_PID" "$GEN_PID" "$STUB_PID"; do
    [[ -n "$pid" ]] && { kill -TERM "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; }
  done
  rm -rf "$WORK"
}
trap cleanup EXIT

GPORT=18580
APORT=18582
QPORT=18591
GAPI="http://127.0.0.1:$GPORT"
AAPI="http://127.0.0.1:$APORT"

AVSYNC_ROLE=generator AVSYNC_EVENT_RATE=2 AVSYNC_STATE_DIR="$WORK/gstate" NMOS_ENABLE=false NMOS_SEED=chain-gen \
  MXL_DOMAIN_SCAN_PATH="$WORK" MXL_OUTPUT_DOMAIN_DIR="$WORK/gen" WEB_PORT="$GPORT" \
  AVSYNC_PTP_STATUS_URL="http://127.0.0.1:$QPORT/ptp" \
  "$BIN" serve >"$WORK/gen.log" 2>&1 &
GEN_PID=$!

python3 - "$QPORT" "$GAPI" <<'PY' >"$WORK/stub.log" 2>&1 &
import json, sys, urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
port, gapi = int(sys.argv[1]), sys.argv[2]
TAG = "urn:x-mxl:tag:avsync-emit-offset-ms/v1.0"
PTP = {"sync_state": "locked", "accuracy_ns": "4048", "grandmaster": "ec4670.fffe.00fe77", "port_state": "SLAVE",
       "ptp4l_offset_ns": -126, "phc2sys_offset_ns": -96, "path_delay_ns": 7670}
class Handler(BaseHTTPRequestHandler):
    def send(self, code, body):
        data = json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)
    def do_GET(self):
        if self.path == "/ptp":
            return self.send(200, PTP)
        prefix = "/x-nmos/query/v1.3/flows/"
        if self.path.startswith(prefix):
            status = json.load(urllib.request.urlopen(gapi + "/api/v1/status", timeout=2))
            emit = status["emit_offsets"]
            flow = self.path[len(prefix):]
            for key, offset in (("video_flow", emit["video_ms"]), ("audio_flow", emit["audio_ms"])):
                if status.get(key) == flow:
                    return self.send(200, {"id": flow, "tags": {TAG: ["%g" % offset]}})
        self.send(404, {"code": 404})
    def log_message(self, *args):
        pass
ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
PY
STUB_PID=$!

for _ in $(seq 1 80); do curl -sf "$GAPI/api/v1/status" >/dev/null && break; sleep 0.1; done

AVSYNC_ROLE=analyzer AVSYNC_EVENT_RATE=2 AVSYNC_STATE_DIR="$WORK/astate" NMOS_ENABLE=false NMOS_SEED=chain-ana \
  NMOS_QUERY_ADDRESS=127.0.0.1 NMOS_QUERY_PORT="$QPORT" AVSYNC_PTP_STATUS_URL="http://127.0.0.1:$QPORT/ptp" \
  MXL_DOMAIN_SCAN_PATH="$WORK" MXL_OUTPUT_DOMAIN_DIR="$WORK/ana" WEB_PORT="$APORT" \
  "$BIN" serve >"$WORK/ana.log" 2>&1 &
ANA_PID=$!

python3 - "$GAPI" "$AAPI" <<'PY'
import json, sys, time, urllib.request
gapi, aapi = sys.argv[1], sys.argv[2]
get = lambda url: json.load(urllib.request.urlopen(url, timeout=2))
deadline = time.time() + 40
chain = []
while time.time() < deadline:
    time.sleep(1)
    try:
        events = get(aapi + "/api/v1/events?limit=100").get("events", [])
    except Exception:
        continue
    chain = [e for e in events if e.get("video_chain_latency_ms") is not None and e.get("audio_chain_latency_ms") is not None]
    if len(chain) >= 6:
        break
gen = get(gapi + "/api/v1/status")["emit_offsets"]
assert gen["video_ms"] == -20 and gen["audio_ms"] == -25, gen
assert gen["video_commit_vs_timestamp_ms"]["p50"] < 0, gen
print("generator emit offsets", gen["video_ms"], gen["audio_ms"], "commit p50", round(gen["video_commit_vs_timestamp_ms"]["p50"], 2))
status = get(aapi + "/api/v1/status")
assert status["source_emit_offsets"]["video_ms"] == -20 and status["source_emit_offsets"]["audio_ms"] == -25, status["source_emit_offsets"]
ptp = status["ptp"]
assert ptp["sync_state"] == "locked" and ptp["gm_present"] and ptp["gm_identity"] == "ec4670fffe00fe77", ptp
assert ptp["port_state"] == "SLAVE" and ptp["path_delay_ns"] == 7670 and ptp["ptp4l_offset_ns"] == -126 and ptp["phc2sys_offset_ns"] == -96, ptp
assert len(chain) >= 6, f"only {len(chain)} events with chain latency"
for e in chain:
    assert e["video_source_emit_offset_ms"] == -20 and e["audio_source_emit_offset_ms"] == -25, e
    assert abs(e["video_chain_latency_ms"] - (e["video_latency_ms"] + 20)) < 1e-6, e
    # Direct path: the chain adds only the analyzer's own read time (video) and one audio block (audio).
    assert -5 <= e["video_chain_latency_ms"] <= 25, e
    assert -15 <= e["audio_chain_latency_ms"] <= 25, e
m = get(aapi + "/api/v1/measurement")
assert m["labels"]["av_sync_ms"] == "AV sync (what MXL consumers see)", m
assert m["av_sync_ms"] == m["offset_ms"] and "video_chain_latency_ms" in m and "video_arrival_vs_timestamp_ms" in m, m
print("chain latency video", round(chain[0]["video_chain_latency_ms"], 2), "audio", round(chain[0]["audio_chain_latency_ms"], 2),
      "from", len(chain), "events")
PY
echo "chain-latency.sh ok"
