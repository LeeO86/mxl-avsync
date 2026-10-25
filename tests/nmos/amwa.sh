#!/usr/bin/env bash
# AMWA nmos-testing against mxl-avsync: IS-04-01, IS-04-02, IS-05-01, IS-05-02.
# DNS-SD stays off. An nmos-cpp registry container is started unless one is already up.
#
#   tests/nmos/amwa.sh off <mxl-avsync>
#
# Needs Docker, python3, an nmos-testing checkout at $NMOS_TESTING_DIR (default
# /tmp/nmos-testing) with $NMOS_TESTING_PYTHON (default /tmp/nmos-venv/bin/python),
# and the kernel TAI offset set (CLOCK_TAI = UTC + 37 s).
# Results go to $AMWA_OUT. tests/nmos/check_amwa.py fails on any result that is
# not Pass and not listed in tests/nmos/amwa-expected-off.json.
set -euo pipefail

MODE="${1:?usage: amwa.sh off <mxl-avsync>}"
NODE_BIN="${2:?usage: amwa.sh off <mxl-avsync>}"
if [[ "$MODE" != off ]]; then
  echo "only DNS-SD off is supported" >&2
  exit 2
fi
HERE="$(cd "$(dirname "$0")" && pwd)"
TOOL="${NMOS_TESTING_DIR:-/tmp/nmos-testing}"
PY="${NMOS_TESTING_PYTHON:-/tmp/nmos-venv/bin/python}"
OUT="${AMWA_OUT:-$HERE/../ci-artifacts/amwa-$MODE}"
REGISTRY_IMAGE="${NMOS_REGISTRY_IMAGE:-mirror.gcr.io/rhastie/nmos-cpp@sha256:c54c8ded3b61740dbd579c03892b0229842250417aa397d87a92e2b91ba645b8}"
NODE_PORT="${AMWA_NODE_PORT:-3352}"
WEB_PORT="${AMWA_WEB_PORT:-8195}"
REG_PORT="${AMWA_REGISTRY_PORT:-8010}"
QUERY_PORT=$((REG_PORT + 1))
ROOTS=()
HOST_IP="${HOST_IP:-$(ip -4 route get 1.1.1.1 | sed -n 's/.* src \([0-9.]*\).*/\1/p')}"
mkdir -p "$OUT"
NODE_PID=""

log() { echo "[amwa-$MODE] $*"; }

wait_http() {
  local url="$1" seconds="$2"
  for _ in $(seq 1 $((seconds * 5))); do
    curl -fsS -o /dev/null "$url" 2>/dev/null && return 0
    sleep 0.2
  done
  log "timeout waiting for $url"
  return 1
}

stop_node() {
  if [[ -n "$NODE_PID" ]] && kill -0 "$NODE_PID" 2>/dev/null; then
    kill -TERM "$NODE_PID"
    wait "$NODE_PID" || true
  fi
  NODE_PID=""
}

cleanup() {
  stop_node
  if [[ -z "${AMWA_REGISTRY_EXTERNAL:-}" ]]; then
    docker logs mxl-avsync-amwa-registry >"$OUT/registry.log" 2>&1 || true
    docker rm -f mxl-avsync-amwa-registry >/dev/null 2>&1 || true
  fi
  for root in "${ROOTS[@]}"; do rm -rf "$root"; done
}
trap cleanup EXIT

start_node() {
  local root
  root=$(mktemp -d /dev/shm/avs-amwa.XXXXXX)
  ROOTS+=("$root")
  mkdir -p "$root/mxl" "$root/state" "$root/mxl/generator"
  NMOS_SEED=amwa-ci NMOS_ENABLE=true NMOS_DNS_SD=false \
    NMOS_HOST_ADDRESS="$HOST_IP" NMOS_PORT="$NODE_PORT" WEB_PORT="$WEB_PORT" \
    NMOS_REGISTRY_ADDRESS="$HOST_IP" NMOS_REGISTRY_PORT="$REG_PORT" \
    AVSYNC_ROLE=generator AVSYNC_STATE_DIR="$root/state" \
    MXL_DOMAIN_SCAN_PATH="$root/mxl" MXL_OUTPUT_DOMAIN_DIR="$root/mxl/generator" \
    MXL_CLEANUP_ON_EXIT=true \
    "$NODE_BIN" serve >"$OUT/node-$1.log" 2>&1 &
  NODE_PID=$!
  wait_http "http://$HOST_IP:$NODE_PORT/x-nmos/node/v1.3/self" 30
  wait_http "http://127.0.0.1:$WEB_PORT/readyz" 30
}

for port in "$NODE_PORT" "$WEB_PORT"; do
  if (exec 3<>"/dev/tcp/127.0.0.1/$port") 2>/dev/null; then
    log "port $port is in use"
    exit 1
  fi
done

if [[ -z "${AMWA_REGISTRY_EXTERNAL:-}" ]]; then
  docker rm -f mxl-avsync-amwa-registry >/dev/null 2>&1 || true
  for port in $(seq "$REG_PORT" $((REG_PORT + 9))); do
    if (exec 3<>"/dev/tcp/127.0.0.1/$port") 2>/dev/null; then
      log "port $port is in use"
      exit 1
    fi
  done
  python3 - "$HERE/registry.json" "$REG_PORT" >"$OUT/registry.json" <<'EOF'
import json, sys
config = json.load(open(sys.argv[1]))
base = int(sys.argv[2])
for index, name in enumerate(["registration_port", "query_port", "query_ws_port", "node_port", "system_port",
                              "settings_port", "logging_port", "admin_port", "mdns_port", "schemas_port"]):
    config[name] = base + index
print(json.dumps(config, indent=2))
EOF
  docker run -d --name mxl-avsync-amwa-registry --network host \
    -v "$OUT/registry.json:/home/registry.json:ro" "$REGISTRY_IMAGE" >/dev/null
fi
wait_http "http://$HOST_IP:$QUERY_PORT/x-nmos/query/v1.3/nodes" 60
cat >"$TOOL/nmostesting/UserConfig.py" <<EOF
from . import Config as CONFIG
CONFIG.ENABLE_DNS_SD = False
CONFIG.QUERY_API_HOST = "$HOST_IP"
CONFIG.QUERY_API_PORT = $QUERY_PORT
EOF

declare -A ARGS=(
  [IS-04-01]="--host $HOST_IP --port $NODE_PORT --version v1.3"
  [IS-04-02]="--host $HOST_IP --port $NODE_PORT --version v1.3"
  [IS-05-01]="--host $HOST_IP --port $NODE_PORT --version v1.2"
  [IS-05-02]="--host $HOST_IP $HOST_IP --port $NODE_PORT $NODE_PORT --version v1.3 v1.2"
)
for suite in IS-04-01 IS-04-02 IS-05-01 IS-05-02; do
  start_node "$suite"
  log "running $suite"
  set +e
  # shellcheck disable=SC2086
  (cd "$TOOL" && "$PY" nmos-test.py suite "$suite" --selection all ${ARGS[$suite]} \
    --output "$OUT/$suite.json") >"$OUT/$suite.log" 2>&1
  rc=$?
  set -e
  log "$suite exited $rc"
  stop_node
done

node_id=$(python3 -c "import uuid; print(uuid.uuid5(uuid.UUID('6ba7b811-9dad-11d1-80b4-00c04fd430c8'), 'mxl-avsync/amwa-ci/node'))")
code=$(curl -s -o /dev/null -w '%{http_code}' "http://$HOST_IP:$QUERY_PORT/x-nmos/query/v1.3/nodes/$node_id")
devices=$(curl -fsS "http://$HOST_IP:$QUERY_PORT/x-nmos/query/v1.3/devices" |
  python3 -c "import json,sys; print(sum(d.get('node_id') == sys.argv[1] for d in json.load(sys.stdin)))" "$node_id")
log "after SIGTERM: Query API node $node_id -> HTTP $code, devices left: $devices"
[[ "$code" == 404 && "$devices" == 0 ]] || { log "FAIL: the node did not deregister"; exit 1; }

python3 "$HERE/check_amwa.py" "$OUT" "$HERE/amwa-expected-off.json"
