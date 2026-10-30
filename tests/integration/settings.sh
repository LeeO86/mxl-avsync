#!/usr/bin/env bash
# Settings: grouped keys, env-set read-only, validation, restart_required, export and import.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${BIN:-$ROOT/build/mxl-avsync}"
export LD_LIBRARY_PATH="${MXL_LIB_DIR:-/opt/mxl/lib}${VCPKG_LIB_DIR:+:$VCPKG_LIB_DIR}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
[[ -x "$BIN" ]] || { echo "missing $BIN" >&2; exit 1; }

WORK="$(mktemp -d /dev/shm/avs-set-XXXXXX)"
PID=""
cleanup() {
  [[ -n "$PID" ]] && { kill -TERM "$PID" 2>/dev/null || true; wait "$PID" 2>/dev/null || true; }
  rm -rf "$WORK"
}
trap cleanup EXIT
mkdir -p "$WORK/state" "$WORK/mxl"
PORT=18790
NPORT=18792
AVSYNC_ROLE=generator AVSYNC_AV_OFFSET_MS=0 AVSYNC_EVENT_RATE=2 AVSYNC_AUDIO_TIMECODE=1200 NMOS_ENABLE=false WEB_PORT=$PORT NMOS_PORT=$NPORT \
  AVSYNC_STATE_DIR="$WORK/state" MXL_DOMAIN_SCAN_PATH="$WORK/mxl" MXL_OUTPUT_DOMAIN_DIR="$WORK/mxl/gen" \
  "$BIN" serve >"$WORK/log" 2>&1 &
PID=$!
for _ in $(seq 1 50); do
  curl -sf "http://127.0.0.1:$PORT/livez" >/dev/null && break
  sleep 0.1
done
curl -sf "http://127.0.0.1:$PORT/" | python3 -c '
import sys
page = sys.stdin.read()
for word in ("Grading", "Signal", "Measurement", "Restart", "Apply", "Export", "Import", "AVSYNC_PASS_MIN_MS"):
    if word not in page:
        sys.exit("settings page missing " + word)
print("settings page")
'
python3 - "http://127.0.0.1:$PORT" <<'PY'
import json, sys, urllib.request
base = sys.argv[1]
request = urllib.request.Request(base + "/api/v1/config")
with urllib.request.urlopen(request) as response:
    body = json.load(response)
    env = set(filter(None, (response.headers.get("X-Env-Set") or "").split(",")))
    live = set(filter(None, (response.headers.get("X-Live-Keys") or "").split(",")))
if "AVSYNC_AV_OFFSET_MS" not in body or "AVSYNC_PASS_MIN_MS" not in body:
    sys.exit("config missing grouped keys")
if "AVSYNC_ROLE" not in env or "AVSYNC_AV_OFFSET_MS" not in env:
    sys.exit("env-set header " + ",".join(sorted(env)))
bad = urllib.request.Request(base + "/api/v1/config", data=json.dumps({
    "AVSYNC_PASS_MIN_MS": "20", "AVSYNC_PASS_MAX_MS": "-5"
}).encode(), method="PUT", headers={"Content-Type": "application/json"})
try:
    urllib.request.urlopen(bad)
    sys.exit("invalid windows were accepted")
except urllib.error.HTTPError as error:
    if error.code != 400:
        sys.exit("expected 400, got " + str(error.code))
    detail = json.load(error)
    if "error" not in detail:
        sys.exit("validation body " + str(detail))
level = urllib.request.Request(base + "/api/v1/config", data=json.dumps({"LOG_LEVEL": "debug"}).encode(),
    method="PUT", headers={"Content-Type": "application/json"})
with urllib.request.urlopen(level) as response:
    applied = json.load(response)
if applied.get("restart_required") != []:
    sys.exit("log level asked for a restart " + str(applied))
with urllib.request.urlopen(base + "/api/v1/config") as response:
    after = json.load(response)
if after["LOG_LEVEL"] != "debug":
    sys.exit("log level did not apply")
restart = urllib.request.Request(base + "/api/v1/config", data=json.dumps({"AVSYNC_ROLE": "analyzer"}).encode(),
    method="PUT", headers={"Content-Type": "application/json"})
with urllib.request.urlopen(restart) as response:
    listed = json.load(response)
if "AVSYNC_ROLE" not in listed.get("restart_required", []):
    sys.exit("restart_required " + str(listed))
with urllib.request.urlopen(base + "/api/v1/config") as response:
    still = json.load(response)
if still["AVSYNC_ROLE"] != "generator":
    sys.exit("role changed without a restart")
payload = json.dumps({"settings": {"LOG_LEVEL": "info", "AVSYNC_ROLE": "analyzer", "AVSYNC_AV_OFFSET_MS": "40"}}).encode()
imported = urllib.request.Request(base + "/api/v1/config/import", data=payload, method="POST",
    headers={"Content-Type": "application/json"})
with urllib.request.urlopen(imported) as response:
    result = json.load(response)
skipped = set(result.get("skipped", []))
if "AVSYNC_ROLE" not in skipped or "AVSYNC_AV_OFFSET_MS" not in skipped:
    sys.exit("import skipped " + str(result))
with urllib.request.urlopen(base + "/api/v1/config") as response:
    final = json.load(response)
if final["LOG_LEVEL"] != "info" or final["AVSYNC_AV_OFFSET_MS"].startswith("40"):
    sys.exit("import result " + final["LOG_LEVEL"] + " " + final["AVSYNC_AV_OFFSET_MS"])
# Env-set event rate and audio timecode are start values: the Signal API changes them live.
def patch_generator(body):
    req = urllib.request.Request(base + "/api/v1/generator", data=json.dumps(body).encode(), method="PATCH",
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req) as response:
        return json.load(response)
if "AVSYNC_EVENT_RATE" not in env or "AVSYNC_AUDIO_TIMECODE" not in env:
    sys.exit("signal keys not env-set " + ",".join(sorted(env)))
if not {"AVSYNC_EVENT_RATE", "AVSYNC_AUDIO_TIMECODE", "AVSYNC_AV_OFFSET_MS"} <= live or "AVSYNC_ROLE" in live:
    sys.exit("live-keys header " + ",".join(sorted(live)))
patch_generator({"event_rate": 1, "audio_timecode": "600"})
signal = json.load(urllib.request.urlopen(base + "/api/v1/generator"))
if signal["event_rate"] != 1 or signal["audio_timecode"] != "600":
    sys.exit("env-set signal keys ignored " + str(signal))
patch_generator({"event_rate": 2, "audio_timecode": "1200"})
signal = json.load(urllib.request.urlopen(base + "/api/v1/generator"))
if signal["event_rate"] != 2 or signal["audio_timecode"] != "1200":
    sys.exit("signal keys did not return " + str(signal))
export = json.load(urllib.request.urlopen(base + "/api/v1/config/export"))
if export.get("secrets") is not False or "AVSYNC_PASS_MIN_MS" not in export.get("settings", {}):
    sys.exit("export " + str(export)[:200])
print("settings.sh ok")
PY
