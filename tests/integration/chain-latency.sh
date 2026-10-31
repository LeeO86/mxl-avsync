#!/usr/bin/env bash
# Generator emit offsets and the analyzer's absolute latency. Source emit offsets: entered
# (AVSYNC_SOURCE_EMIT_*_MS, live via PUT /api/v1/config) win; else the 2.0.2 flow tag from the Query
# API; else none. A Python stand-in serves the Query API flows and the extended PTP status page.
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

touch "$WORK/hide"
python3 - "$QPORT" "$GAPI" "$WORK/hide" <<'PY' >"$WORK/stub.log" 2>&1 &
import json, os, sys, urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
port, gapi, hide = int(sys.argv[1]), sys.argv[2], sys.argv[3]
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
        if self.path.startswith(prefix) and not os.path.exists(hide):
            status = json.load(urllib.request.urlopen(gapi + "/api/v1/status", timeout=2))
            emit = status["emit_offsets"]
            flow = self.path[len(prefix):]
            # The real tag is set once at registration from the nominal values.
            for key, offset in (("video_flow", emit["video_ms"]), ("audio_flow", emit["audio_nominal_ms"])):
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

python3 - "$GAPI" "$AAPI" "$WORK/hide" <<'PY'
import json, sys, time, urllib.request
import os
gapi, aapi, hide = sys.argv[1], sys.argv[2], sys.argv[3]
get = lambda url: json.load(urllib.request.urlopen(url, timeout=2))
def patch(url, body):
    req = urllib.request.Request(url, data=json.dumps(body).encode(), method="PUT", headers={"Content-Type": "application/json"})
    return urllib.request.urlopen(req, timeout=2).status
def events():
    return get(aapi + "/api/v1/events?limit=100").get("events", [])
deadline = time.time() + 30
while time.time() < deadline and len([e for e in events() if e.get("video_latency_ms") is not None]) < 4:
    time.sleep(1)
# Nothing entered: no absolute latency, only "relative to the source timestamp".
m = get(aapi + "/api/v1/measurement")
assert m["latency_basis"] == "relative to the source timestamp" and "video_latency_abs_ms" not in m, m
assert m["source_emit_offsets"]["source"] == "none" and m["clock"]["source"] == "url" and m["clock"]["accuracy_ms"] == 0.004048, m
assert all("video_latency_abs_ms" not in e for e in events())
gen = get(gapi + "/api/v1/status")["emit_offsets"]
assert gen["video_ms"] == -20 and gen["audio_nominal_ms"] == -25 and gen["nmos_tag"] == "urn:x-mxl:tag:avsync-emit-offset-ms/v1.0", gen
assert gen["video_commit_vs_timestamp_ms"]["p50"] < 0, gen
# The 2.0.2 tag of the directly connected generator flow (read from the Query API) gives the offsets.
os.remove(hide)
tagged = []
deadline = time.time() + 45  # the lookup refreshes every 30 s
while time.time() < deadline and len(tagged) < 4:
    time.sleep(1)
    tagged = [e for e in events() if e.get("video_latency_abs_ms") is not None]
assert len(tagged) >= 4 and all(e["video_source_emit_offset_ms"] == -20 and e["audio_source_emit_offset_ms"] == -25 for e in tagged), tagged[:1]
# Tag (nominal -25): the direct link reads audio chain 0..+1 ms, never a mismatch.
chains = sorted(e["audio_chain_latency_ms"] for e in tagged)
assert 0 <= chains[len(chains) // 2] <= 2 and all("emit_offset_mismatch" not in e.get("flags", []) for e in tagged), chains
m = get(aapi + "/api/v1/measurement")
assert m["source_emit_offsets"]["source"] == "nmos-tag" and m["source_emit_offsets"]["video_source"] == "nmos-tag" and m["latency_basis"] == "absolute", m
# Entered values win over the tag (here the same numbers, so the source label is what changes).

gen = get(gapi + "/api/v1/status")["emit_offsets"]
# Each event's audio onset is committed at a fixed offset: within 0.5 ms of the nominal (rendered ahead, committed on time).
onset = gen["audio_commit_vs_onset_ms"]
assert -25 <= onset["p50"] <= -24.5 and onset["max"] - onset["min"] < 0.5 and abs(gen["audio_ms"] - onset["p50"]) < 0.01, gen
assert patch(aapi + "/api/v1/config", {"AVSYNC_SOURCE_EMIT_VIDEO_MS": str(gen["video_ms"]), "AVSYNC_SOURCE_EMIT_AUDIO_MS": str(gen["audio_ms"])}) in (200, 204)
since = time.time()
absolute = []
deadline = time.time() + 30
while time.time() < deadline and len(absolute) < 6:
    time.sleep(1)
    absolute = [e for e in events() if e.get("video_latency_abs_ms") is not None and e.get("audio_latency_abs_ms") is not None and abs(e.get("audio_source_emit_offset_ms", 0) - gen["audio_ms"]) < 1e-9]
assert len(absolute) >= 6, f"only {len(absolute)} events with absolute latency"
for e in absolute:
    assert e["video_source_emit_offset_ms"] == -20 and abs(e["audio_source_emit_offset_ms"] - gen["audio_ms"]) < 1e-9, e
    assert e["video_latency_abs_ms"] >= 0 and e["audio_latency_abs_ms"] >= 0, e
    assert abs(e["video_chain_latency_ms"] - (e["video_latency_ms"] + 20)) < 1e-6, e
    # Direct path: the chain adds only the analyzer's own read time (video) and the audio block (audio).
    assert e["video_latency_abs_ms"] <= 25 and e["audio_latency_abs_ms"] <= 25, e
    assert e["latency_abs_clock_accuracy_known"] is True and abs(e["video_latency_abs_uncertainty_ms"] - 1.004048) < 1e-6, e
    # Entered = the generator's measured onset commit: the direct link reads audio chain 0..+1 ms.
    assert -0.5 <= e["audio_chain_latency_ms"] <= 1.5, e
    assert "emit_offset_mismatch" not in e.get("flags", []), e
m = get(aapi + "/api/v1/measurement")
assert m["latency_basis"] == "absolute" and m["source_emit_offsets"]["source"] == "entered" and "video_latency_abs_ms" in m, m
assert m["labels"]["av_sync_ms"] == "AV sync (what MXL consumers see)" and m["av_sync_ms"] == m["offset_ms"], m
# A wrong entry (the source emits 60 ms later than entered) is flagged, never a negative latency.
assert patch(aapi + "/api/v1/config", {"AVSYNC_SOURCE_EMIT_VIDEO_MS": "40"}) in (200, 204)
flagged = []
deadline = time.time() + 20
while time.time() < deadline and not flagged:
    time.sleep(1)
    flagged = [e for e in events() if e.get("video_source_emit_offset_ms") == 40]
assert flagged and all("video_latency_abs_ms" not in e and "emit_offset_mismatch" in e["flags"] for e in flagged), flagged[:1]
metrics = {}
for line in urllib.request.urlopen(aapi + "/metrics", timeout=2).read().decode().splitlines():
    if line and not line.startswith("#"):
        name, _, value = line.rpartition(" ")
        metrics[name] = float(value)
assert 0 <= metrics['mxl_avsync_latency_abs_ms{essence="video"}'] <= 25, metrics
assert metrics['mxl_avsync_latency_abs_uncertainty_ms{essence="audio"}'] > 1.5, metrics
assert metrics['mxl_avsync_latency_abs_seconds_count{essence="video"}'] >= 6, metrics
assert metrics['mxl_avsync_latency_abs_seconds_bucket{essence="video",le="0.05"}'] == metrics['mxl_avsync_latency_abs_seconds_count{essence="video"}'], metrics
# AV offset gauge: median over the grade window; the histogram counts every paired event, like events_total.
assert abs(metrics["mxl_avsync_av_offset_ms"]) < 0.1, metrics
assert metrics["mxl_avsync_av_offset_seconds_count"] == metrics['mxl_avsync_events_total{result="paired"}'] >= 10, metrics
assert metrics['mxl_avsync_av_offset_seconds_bucket{le="0.005"}'] == metrics["mxl_avsync_av_offset_seconds_count"], metrics
assert metrics['mxl_avsync_emit_offset_mismatch_total{essence="video"}'] >= 1 and metrics['mxl_avsync_emit_offset_mismatch_total{essence="audio"}'] == 0, metrics
ptp = get(aapi + "/api/v1/status")["ptp"]
assert ptp["gm_identity"] == "ec4670fffe00fe77" and ptp["port_state"] == "SLAVE" and ptp["path_delay_ns"] == 7670, ptp
print("absolute latency video", round(absolute[0]["video_latency_abs_ms"], 2), "±", round(absolute[0]["video_latency_abs_uncertainty_ms"], 3),
      "audio", round(absolute[0]["audio_latency_abs_ms"], 2), "±", round(absolute[0]["audio_latency_abs_uncertainty_ms"], 3), "from", len(absolute), "events")
PY
echo "chain-latency.sh ok"
