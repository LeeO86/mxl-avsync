#!/usr/bin/env bash
# File-path checks from SPECIFICATION §14.2. Duration is short so CI stays practical;
# the event grid is the same one the 60 s example uses.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${BIN:-$ROOT/build/mxl-avsync}"
WORK="${WORK:-$(mktemp -d)}"
trap 'rm -rf "$WORK"' EXIT

check_offset() {
  local name="$1" offset="$2" format="${3:-1080p50}" extra=()
  shift 3 || true
  extra=("$@")
  local out="$WORK/$name.mov"
  "$BIN" generate --format "$format" --duration 3 --offset "$offset" --test-id "TEST-$name" --out "$out" "${extra[@]}"
  "$BIN" analyze "$out" --manifest "$out.manifest.json" --json "$WORK/$name.json" >/tmp/avsync-analyze.txt
  python3 - "$WORK/$name.json" "$offset" <<'PY'
import json, sys
doc = json.load(open(sys.argv[1]))
expect = float(sys.argv[2])
stats = doc["statistics"]["window"]
dev = doc["statistics"]["deviation"]
assert stats["valid"] >= 3, stats
assert abs(stats["median"] - expect) <= 1.0, (stats["median"], expect)
assert abs(dev["median"]) <= 1.0, dev["median"]
print(f"ok {sys.argv[1]} median={stats['median']} deviation={dev['median']} valid={stats['valid']}")
PY
}

check_offset zero 0 1080p50
check_offset plus 35 720p50
check_offset minus -35 720p50

"$BIN" generate --format 720p50 --duration 3 --offset-sweep -40,-20,0,20,40 --test-id TEST-sweep --out "$WORK/sweep.mov"
"$BIN" analyze "$WORK/sweep.mov" --manifest "$WORK/sweep.mov.manifest.json" --json "$WORK/sweep.json" >/dev/null
python3 - "$WORK/sweep.json" <<'PY'
import json, sys
doc = json.load(open(sys.argv[1]))
devs = [e["deviation_ms"] for e in doc["events"] if e.get("deviation_ms") is not None]
assert len(devs) >= 3 and max(abs(d) for d in devs) <= 1.0, devs
print("ok sweep", devs)
PY

"$BIN" validate "$WORK/zero.mov.manifest.json"
"$BIN" validate "$WORK/zero.json"
"$BIN" merge "$WORK/zero.json" "$WORK/plus.json" --json "$WORK/merge.json"
python3 - "$WORK/merge.json" <<'PY'
import json, sys
doc = json.load(open(sys.argv[1]))
assert doc["events"], doc
assert all(e["valid"] is False for e in doc["events"])
print("ok merge refuses file timelines")
PY

# Inserted audio delay on a copy. Video is copied; audio is delayed 20 ms.
if command -v ffmpeg >/dev/null; then
  ffmpeg -y -v error -i "$WORK/zero.mov" -c:v copy -af "adelay=20:all=1" "$WORK/delay.mov"
  cp "$WORK/zero.mov.manifest.json" "$WORK/delay.mov.manifest.json"
  "$BIN" analyze "$WORK/delay.mov" --manifest "$WORK/delay.mov.manifest.json" --json "$WORK/delay.json" >/dev/null
  python3 - "$WORK/delay.json" <<'PY'
import json, sys
doc = json.load(open(sys.argv[1]))
med = doc["statistics"]["window"]["median"]
assert abs(med - 20) <= 2.0, med
print("ok delay", med)
PY
  "$BIN" generate --format 720p50 --duration 3 --offset 0 --container mp4 --video-codec h264 --audio-codec aac \
    --allow-container-shift --test-id TEST-h264 --out "$WORK/h264.mp4"
  "$BIN" analyze "$WORK/h264.mp4" --manifest "$WORK/h264.mp4.manifest.json" --json "$WORK/h264.json" >/dev/null || true
  python3 - "$WORK/h264.json" <<'PY'
import json, sys
doc = json.load(open(sys.argv[1]))
print("h264 valid", doc["statistics"]["window"]["valid"], "median", doc["statistics"]["window"].get("median"))
PY
fi

echo "file integration ok"
