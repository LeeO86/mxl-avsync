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

AVSYNC_ROLE=generator AVSYNC_EVENT_RATE=2 AVSYNC_AV_OFFSET_MS=0 NMOS_SEED=nmos-gen \
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
NODE=$(python3 -c 'import json,sys; print(json.loads(sys.argv[1])["id"])' "$SELF")
python3 -c 'import hashlib,sys,uuid; name=sys.argv[1]; ns=uuid.UUID("6ba7b811-9dad-11d1-80b4-00c04fd430c8"); print(uuid.uuid5(ns, name))' "mxl-avsync/nmos-gen/node" | grep -q "$NODE"
echo "node id $NODE"
SENDERS=$(curl -sf "http://127.0.0.1:$NPORT/x-nmos/node/v1.3/senders")
echo "$SENDERS" | grep -q 'urn:x-nmos:transport:mxl'
echo "$SENDERS" | grep -q 'AVSync Test Video'
FLOWS=$(curl -sf "http://127.0.0.1:$NPORT/x-nmos/node/v1.3/flows")
echo "$FLOWS" | grep -q 'video/v210'
echo "$FLOWS" | grep -q 'audio/float32'
VID=$(python3 -c 'import json,sys; flows=json.loads(sys.argv[1]); print(next(f["id"] for f in flows if f.get("media_type")=="video/v210" and "Result" not in f.get("label","")))' "$FLOWS")
DOM=$(curl -sf "http://127.0.0.1:$NPORT/x-nmos/connection/v1.2/single/senders/$(python3 -c 'import json,sys; ss=json.loads(sys.argv[1]); print(next(s["id"] for s in ss if s.get("label")=="AVSync Test Video"))' "$SENDERS")/active")
echo "$DOM" | grep -q '"mxl_flow_id"'
DOMAIN=$(python3 -c 'import json,sys; print(json.loads(sys.argv[1])["transport_params"][0]["mxl_domain_id"])' "$DOM")
echo "sender flow $VID domain $DOMAIN"

for _ in $(seq 1 50); do
  curl -sf "http://127.0.0.1:$GPORT/readyz" >/dev/null && break
  sleep 0.1
done

AVSYNC_ROLE=analyzer AVSYNC_EVENT_RATE=2 AVSYNC_AV_OFFSET_MS=0 NMOS_SEED=nmos-ana \
  NMOS_ENABLE=true NMOS_DNS_SD=false NMOS_REGISTRY_ADDRESS= \
  NMOS_HOST_ADDRESS=127.0.0.1 NMOS_PORT=$ANPORT WEB_PORT=$APORT \
  AVSYNC_STATE_DIR="$WORK/a" MXL_DOMAIN_SCAN_PATH="$WORK" MXL_OUTPUT_DOMAIN_DIR="$WORK/ana-out" \
  "$BIN" serve >"$WORK/ana.log" 2>&1 &
APID=$!
for _ in $(seq 1 80); do
  curl -sf "http://127.0.0.1:$ANPORT/x-nmos/node/v1.3/receivers" >/dev/null && break
  sleep 0.1
done
RECS=$(curl -sf "http://127.0.0.1:$ANPORT/x-nmos/node/v1.3/receivers")
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
  code=$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$APORT/readyz" || true)
  [[ "$code" == "200" ]] && break
  sleep 0.25
done
test "$code" = 200
echo "analyzer ready after IS-05"
curl -sf "http://127.0.0.1:$APORT/api/v1/status" | grep -q '"input_state":"running"'

set +e
AVSYNC_ROLE=generator NMOS_ENABLE=true NMOS_DNS_SD=false NMOS_PORT=$NPORT WEB_PORT=18690 AVSYNC_STATE_DIR="$WORK/taken" \
  "$BIN" serve >"$WORK/taken.log" 2>&1
taken=$?
set -e
test "$taken" = 75
echo "taken port exits 75"
echo "nmos.sh ok"
