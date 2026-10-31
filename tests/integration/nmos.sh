#!/usr/bin/env bash
# IS-04 node and IS-05 activation. DNS-SD stays off. No registry, so nothing uses mDNS.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${BIN:-$ROOT/build/mxl-avsync}"
export LD_LIBRARY_PATH="${MXL_LIB_DIR:-/opt/mxl/lib}${VCPKG_LIB_DIR:+:$VCPKG_LIB_DIR}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
[[ -x "$BIN" ]] || { echo "missing $BIN" >&2; exit 1; }

WORK="$(mktemp -d /dev/shm/avs-nmos-XXXXXX)"
GPID=""
APID=""
cleanup() {
  [[ -n "$GPID" ]] && { kill -TERM "$GPID" 2>/dev/null || true; wait "$GPID" 2>/dev/null || true; }
  [[ -n "$APID" ]] && { kill -TERM "$APID" 2>/dev/null || true; wait "$APID" 2>/dev/null || true; }
  rm -rf "$WORK"
}
trap cleanup EXIT

GPORT=18680
NPORT=18632
APORT=18682
ANPORT=18634
mkdir -p "$WORK/g" "$WORK/a" "$WORK/gen"

AVSYNC_ROLE=generator AVSYNC_EVENT_RATE=2 AVSYNC_AV_OFFSET_MS=0 NMOS_SEED=nmos-gen NMOS_LABEL="AV sync test label" \
  NMOS_ENABLE=true NMOS_DNS_SD=false NMOS_REGISTRY_ADDRESS= \
  NMOS_HOST_ADDRESS=127.0.0.1 NMOS_PORT=$NPORT WEB_PORT=$GPORT \
  AVSYNC_STATE_DIR="$WORK/g" MXL_DOMAIN_SCAN_PATH="$WORK" MXL_OUTPUT_DOMAIN_DIR="$WORK/gen" \
  "$BIN" serve >"$WORK/gen.log" 2>&1 &
GPID=$!
for _ in $(seq 1 80); do
  curl -sf "http://127.0.0.1:$NPORT/x-nmos/node/v1.3/self" >/dev/null && break
  sleep 0.1
done
SELF=$(curl -sf "http://127.0.0.1:$NPORT/x-nmos/node/v1.3/self")
echo "$SELF" | grep -q '"description":"mxl-avsync"'
python3 - "$SELF" <<'PY'
import json, sys
self = json.loads(sys.argv[1])
host = self["hostname"]
label = self["label"]
if "." in host:
    sys.exit("hostname has a domain suffix: " + host)
if label != "AV sync test label":
    sys.exit("node label is not NMOS_LABEL verbatim: " + label)
print("node", label, host)
PY
NODE=$(python3 -c 'import json,sys; print(json.loads(sys.argv[1])["id"])' "$SELF")
python3 -c 'import hashlib,sys,uuid; name=sys.argv[1]; ns=uuid.UUID("6ba7b811-9dad-11d1-80b4-00c04fd430c8"); print(uuid.uuid5(ns, name))' "mxl-avsync/nmos-gen/node" | grep -q "$NODE"
echo "node id $NODE"
SENDERS=$(curl -sf "http://127.0.0.1:$NPORT/x-nmos/node/v1.3/senders")
echo "$SENDERS" | grep -q 'urn:x-nmos:transport:mxl'
echo "$SENDERS" | grep -q 'AVSync Test Video'
FLOWS=$(curl -sf "http://127.0.0.1:$NPORT/x-nmos/node/v1.3/flows")
echo "$FLOWS" | grep -q 'video/v210'
echo "$FLOWS" | grep -q 'audio/float32'
python3 - "$SELF" "$FLOWS" "$SENDERS" <<'PY'
import json, sys
self = json.loads(sys.argv[1])
flows = json.loads(sys.argv[2])
senders = json.loads(sys.argv[3])
video = next(f for f in flows if f.get("media_type") == "video/v210" and "Result" not in f.get("label", ""))
description = video.get("description", "")
if "50/s" not in description or "offset 0 ms" not in description:
    sys.exit("video flow description: " + description)
if any("urn:x-mxl:tag:avsync-emit-offset-ms/v1.0" in f.get("tags", {}) for f in flows):
    sys.exit("flows still carry the removed emit offset tag")
for resource in flows + senders:
    tags = resource.get("tags", {})
    if tags.get("urn:x-mxl:tag:test-signal/v1.0") != ["mxl-avsync"]:
        sys.exit("missing test-signal tag on " + resource.get("label", ""))
    if resource in senders and not resource.get("interface_bindings"):
        sys.exit("empty interface_bindings on " + resource.get("label", ""))
print("generator content", description, senders[0].get("interface_bindings"))
PY
DEVICES=$(curl -sf "http://127.0.0.1:$NPORT/x-nmos/node/v1.3/devices")
python3 - "$SELF" "$DEVICES" <<'PY'
import json, sys
self = json.loads(sys.argv[1])
devices = json.loads(sys.argv[2])
device = devices[0]
if device.get("tags", {}).get("urn:x-mxl:tag:test-signal/v1.0") != ["mxl-avsync"]:
    sys.exit("device has no test-signal tag")
if device["label"] != self["label"] + " generator":
    sys.exit("device label lacks the role: " + device["label"])
print("device", device["label"])
PY
VID=$(python3 -c 'import json,sys; flows=json.loads(sys.argv[1]); print(next(f["id"] for f in flows if f.get("media_type")=="video/v210" and "Result" not in f.get("label","")))' "$FLOWS")
DOM=$(curl -sf "http://127.0.0.1:$NPORT/x-nmos/connection/v1.2/single/senders/$(python3 -c 'import json,sys; ss=json.loads(sys.argv[1]); print(next(s["id"] for s in ss if s.get("label")=="AVSync Test Video"))' "$SENDERS")/active")
echo "$DOM" | grep -q '"mxl_flow_id"'
DOMAIN=$(python3 -c 'import json,sys; print(json.loads(sys.argv[1])["transport_params"][0]["mxl_domain_id"])' "$DOM")
echo "sender flow $VID domain $DOMAIN"

for _ in $(seq 1 50); do
  curl -sf "http://127.0.0.1:$GPORT/readyz" >/dev/null && break
  sleep 0.1
done

AVSYNC_ROLE=analyzer AVSYNC_EVENT_RATE=2 AVSYNC_AV_OFFSET_MS=0 NMOS_SEED=nmos-ana NMOS_LABEL="AV sync test label" \
  NMOS_ENABLE=true NMOS_DNS_SD=false NMOS_REGISTRY_ADDRESS= \
  NMOS_HOST_ADDRESS=127.0.0.1 NMOS_PORT=$ANPORT WEB_PORT=$APORT \
  AVSYNC_STATE_DIR="$WORK/a" MXL_DOMAIN_SCAN_PATH="$WORK" MXL_OUTPUT_DOMAIN_DIR="$WORK/ana-out" \
  "$BIN" serve >"$WORK/ana.log" 2>&1 &
APID=$!
for _ in $(seq 1 80); do
  curl -sf "http://127.0.0.1:$ANPORT/x-nmos/node/v1.3/receivers" >/dev/null && break
  sleep 0.1
done
python3 - "$ANPORT" <<'PY'
import json,sys,urllib.request
base="http://127.0.0.1:"+sys.argv[1]+"/x-nmos/node/v1.3/"
node=json.load(urllib.request.urlopen(base+"self"))
device=json.load(urllib.request.urlopen(base+"devices"))[0]
assert node["label"] == "AV sync test label", node
assert device["label"] == "AV sync test label analyzer", device
PY
# A platform may wait for this before sending its first IS-05 activation.
for _ in $(seq 1 50); do
  curl -sf "http://127.0.0.1:$APORT/readyz" >/dev/null && break
  sleep 0.1
done
curl -sf "http://127.0.0.1:$APORT/readyz" >/dev/null
curl -sf "http://127.0.0.1:$APORT/statusz" | python3 -c '
import json,sys
s=json.load(sys.stdin)
assert s["input_state"]=="not_routed" and s["media_ready"] is False, s
'
echo "analyzer ready before IS-05, input not_routed"
RECS=$(curl -sf "http://127.0.0.1:$ANPORT/x-nmos/node/v1.3/receivers")
python3 - "$RECS" <<'PY'
import json, sys
receivers = json.loads(sys.argv[1])
video = next(r for r in receivers if r.get("label") == "AVSync In Video")
audio = next(r for r in receivers if r.get("label") == "AVSync In Audio")
vsets = video.get("caps", {}).get("constraint_sets", [])
asets = audio.get("caps", {}).get("constraint_sets", [])
if not any(s.get("urn:x-nmos:cap:format:media_type") == "video/v210" and s.get("urn:x-nmos:cap:format:frame_width") == 1920 for s in vsets):
    sys.exit("video receiver caps missing 1920 v210")
audio_set = asets[0] if asets else {}
if audio_set.get("urn:x-nmos:cap:format:media_type") != "audio/float32":
    sys.exit("audio receiver caps")
rate = audio_set.get("urn:x-nmos:cap:format:sample_rate", {})
if rate.get("numerator") != 48000:
    sys.exit("audio sample rate")
channels = audio_set.get("urn:x-nmos:cap:format:channel_count", {})
if channels.get("minimum") != 1 or channels.get("maximum") != 64:
    sys.exit("audio channel cap")
if not video.get("interface_bindings") or not audio.get("interface_bindings"):
    sys.exit("receiver interface_bindings empty")
print("receiver caps", len(vsets), audio.get("interface_bindings"))
PY
VREC=$(python3 -c 'import json,sys; print(next(r["id"] for r in json.loads(sys.argv[1]) if r.get("label")=="AVSync In Video"))' "$RECS")
AREC=$(python3 -c 'import json,sys; print(next(r["id"] for r in json.loads(sys.argv[1]) if r.get("label")=="AVSync In Audio"))' "$RECS")
AFLOW=$(python3 -c 'import json,sys; print(next(f["id"] for f in json.loads(sys.argv[1]) if f.get("media_type")=="audio/float32"))' "$FLOWS")
activate() {
  local id="$1" flow="$2"
  curl -sf -X PATCH "http://127.0.0.1:$ANPORT/x-nmos/connection/v1.2/single/receivers/$id/staged" \
    -H 'Content-Type: application/json' \
    -d "{\"master_enable\":true,\"activation\":{\"mode\":\"activate_immediate\"},\"transport_params\":[{\"mxl_domain_id\":\"$DOMAIN\",\"mxl_flow_id\":\"$flow\"}]}" >/dev/null
}
activate "$VREC" "$VID"
activate "$AREC" "$AFLOW"
for _ in $(seq 1 40); do
  curl -sf "http://127.0.0.1:$APORT/api/v1/status" | grep -q '"input_state":"running"' && break
  sleep 0.25
done
curl -sf "http://127.0.0.1:$APORT/readyz" >/dev/null
curl -sf "http://127.0.0.1:$APORT/api/v1/status" | grep -q '"input_state":"running"'
echo "analyzer ready after IS-05"
curl -sf "http://127.0.0.1:$APORT/api/v1/status" | grep -q '"input_state":"running"'
SEQ=$(python3 - "http://127.0.0.1:$APORT" <<'PY'
import json, sys, time, urllib.request
api = sys.argv[1]
for _ in range(80):
    with urllib.request.urlopen(api + "/api/v1/events?limit=100") as response:
        events = json.load(response).get("events", [])
    good = [e for e in events if e.get("av_offset_ms") is not None and e.get("grade") != "invalid"]
    if good:
        print(int(good[-1]["seq"]))
        sys.exit(0)
    time.sleep(0.25)
print("events", len(events), file=sys.stderr)
if events:
    print(json.dumps(events[-1]), file=sys.stderr)
sys.exit("no paired event before unroute")
PY
)
echo "paired before unroute $SEQ"
disable() {
  local id="$1"
  curl -sf -X PATCH "http://127.0.0.1:$ANPORT/x-nmos/connection/v1.2/single/receivers/$id/staged" \
    -H 'Content-Type: application/json' \
    -d '{"master_enable":false,"activation":{"mode":"activate_immediate"}}' >/dev/null
}
disable "$VREC"
disable "$AREC"
unrouted=0
for _ in $(seq 1 20); do
  if curl -sf "http://127.0.0.1:$APORT/api/v1/status" | grep -q '"input_state":"not_routed"'; then
    unrouted=1
    break
  fi
  sleep 0.1
done
test "$unrouted" = 1
code=$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$APORT/readyz" || true)
test "$code" = "200"
sleep 1
python3 - "http://127.0.0.1:$APORT" "$SEQ" <<'PY'
import json, sys, urllib.request
api, seq = sys.argv[1], int(sys.argv[2])
with urllib.request.urlopen(f"{api}/api/v1/events?limit=100&since={seq+1}") as response:
    events = json.load(response).get("events", [])
good = [e for e in events if e.get("av_offset_ms") is not None and e.get("grade") != "invalid" and int(e["seq"]) > seq]
if good:
    sys.exit("new paired event after unroute " + str(good[-1]["seq"]))
print("no new events after unroute")
PY
echo "unrouted"

kill -TERM "$GPID"
wait "$GPID" 2>/dev/null || true
GPID=""
rm -rf "$WORK/gen2"
mkdir -p "$WORK/g2" "$WORK/gen2"
AVSYNC_ROLE=generator AVSYNC_EVENT_RATE=2 AVSYNC_AV_OFFSET_MS=0 NMOS_SEED=nmos-gen-2 \
  NMOS_ENABLE=true NMOS_DNS_SD=false NMOS_REGISTRY_ADDRESS= \
  NMOS_HOST_ADDRESS=127.0.0.1 NMOS_PORT=18636 WEB_PORT=18684 \
  AVSYNC_STATE_DIR="$WORK/g2" MXL_DOMAIN_SCAN_PATH="$WORK" MXL_OUTPUT_DOMAIN_DIR="$WORK/gen2" \
  "$BIN" serve >"$WORK/gen2.log" 2>&1 &
GPID=$!
for _ in $(seq 1 80); do
  curl -sf "http://127.0.0.1:18636/x-nmos/node/v1.3/flows" >/dev/null && break
  sleep 0.1
done
FLOWS2=$(curl -sf "http://127.0.0.1:18636/x-nmos/node/v1.3/flows")
SENDERS2=$(curl -sf "http://127.0.0.1:18636/x-nmos/node/v1.3/senders")
VID2=$(python3 -c 'import json,sys; flows=json.loads(sys.argv[1]); print(next(f["id"] for f in flows if f.get("media_type")=="video/v210" and "Result" not in f.get("label","")))' "$FLOWS2")
AFLOW2=$(python3 -c 'import json,sys; print(next(f["id"] for f in json.loads(sys.argv[1]) if f.get("media_type")=="audio/float32"))' "$FLOWS2")
DOM2=$(curl -sf "http://127.0.0.1:18636/x-nmos/connection/v1.2/single/senders/$(python3 -c 'import json,sys; ss=json.loads(sys.argv[1]); print(next(s["id"] for s in ss if s.get("label")=="AVSync Test Video"))' "$SENDERS2")/active")
DOMAIN2=$(python3 -c 'import json,sys; print(json.loads(sys.argv[1])["transport_params"][0]["mxl_domain_id"])' "$DOM2")
test "$VID2" != "$VID"
activate2() {
  local id="$1" flow="$2"
  curl -sf -X PATCH "http://127.0.0.1:$ANPORT/x-nmos/connection/v1.2/single/receivers/$id/staged" \
    -H 'Content-Type: application/json' \
    -d "{\"master_enable\":true,\"activation\":{\"mode\":\"activate_immediate\"},\"transport_params\":[{\"mxl_domain_id\":\"$DOMAIN2\",\"mxl_flow_id\":\"$flow\"}]}" >/dev/null
}
activate2 "$VREC" "$VID2"
activate2 "$AREC" "$AFLOW2"
for _ in $(seq 1 40); do
  curl -sf "http://127.0.0.1:$APORT/api/v1/status" | grep -q '"input_state":"running"' && break
  sleep 0.25
done
curl -sf "http://127.0.0.1:$APORT/readyz" >/dev/null
curl -sf "http://127.0.0.1:$APORT/api/v1/status" | grep -q '"input_state":"running"'
python3 - "http://127.0.0.1:$APORT" "$SEQ" <<'PY'
import json, sys, time, urllib.request
api, seq = sys.argv[1], int(sys.argv[2])
for _ in range(80):
    with urllib.request.urlopen(f"{api}/api/v1/events?limit=100&since={seq+1}") as response:
        events = json.load(response).get("events", [])
    good = [e for e in events if e.get("av_offset_ms") is not None and e.get("grade") != "invalid" and int(e["seq"]) > seq]
    if good:
        print("measured new flow", good[-1]["seq"], good[-1]["av_offset_ms"], good[-1]["grade"])
        sys.exit(0)
    time.sleep(0.25)
sys.exit("no measurement on the new flow")
PY
echo "rerouted"

set +e
AVSYNC_ROLE=generator NMOS_ENABLE=true NMOS_DNS_SD=false NMOS_PORT=18636 WEB_PORT=18690 AVSYNC_STATE_DIR="$WORK/taken" \
  "$BIN" serve >"$WORK/taken.log" 2>&1
taken=$?
set -e
test "$taken" = 75
echo "taken port exits 75"
echo "nmos.sh ok"
