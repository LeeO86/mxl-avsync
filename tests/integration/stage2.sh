#!/usr/bin/env bash
# Stage 2 file-mode paths: fiducial locate, H.264 bitrates, G.711, cross-node merge JSON.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${BIN:-$ROOT/build/mxl-avsync}"
WORK="${WORK:-$(mktemp -d)}"
echo "work $WORK"
if [[ "${KEEP:-}" != 1 ]]; then
  trap 'rm -rf "$WORK"' EXIT
fi

if "$BIN" paths | grep -E 'srt-live|g711-sip|st2110-loop|fabrics-cross-node'; then
  echo "paths still names a sibling transport as a path of this repo" >&2
  exit 1
fi
"$BIN" paths | grep -q 'sibling-gateways'
"$BIN" paths | grep -q 'needs the Linux host'

"$BIN" generate --format 720p50 --duration 2 --offset 0 --test-id TEST-stage2 --out "$WORK/src.mov"

median() {
  python3 - "$1" <<'PY'
import json, sys
doc = json.load(open(sys.argv[1]))
print(doc["statistics"]["window"].get("median"), doc["statistics"]["window"].get("valid"))
PY
}

if ! command -v ffmpeg >/dev/null; then
  echo "ffmpeg missing; skipped transcode rows"
  exit 0
fi

ffmpeg -y -v error -i "$WORK/src.mov" -vf "scale=640:360,pad=960:540:80:40" -c:v prores_ks -profile:v 3 -c:a copy "$WORK/tile.mov"
"$BIN" analyze "$WORK/tile.mov" --locate --manifest "$WORK/src.mov.manifest.json" --json "$WORK/tile.json" >/dev/null
python3 - "$WORK/tile.json" <<'PY'
import json, sys
doc = json.load(open(sys.argv[1]))
med = doc["statistics"]["window"]["median"]
valid = doc["statistics"]["window"]["valid"]
assert valid >= 1, doc["statistics"]["window"]
assert abs(med) <= 2.0, med
print("ok tile locate", med, "valid", valid)
PY

for rate in 1M 4M 10M; do
  ffmpeg -y -v error -i "$WORK/src.mov" -c:v libx264 -b:v "$rate" -c:a pcm_s16le "$WORK/h264-$rate.mkv"
  "$BIN" analyze "$WORK/h264-$rate.mkv" --manifest "$WORK/src.mov.manifest.json" --json "$WORK/h264-$rate.json" >/dev/null || true
  echo -n "h264 $rate "
  median "$WORK/h264-$rate.json"
done
python3 - "$WORK/h264-4M.json" "$WORK/h264-10M.json" <<'PY'
import json, sys
for path in sys.argv[1:]:
    doc = json.load(open(path))
    valid = doc["statistics"]["window"]["valid"]
    assert valid >= 1, (path, valid)
print("ok h264 4M and 10M found events")
PY

for codec in pcm_mulaw pcm_alaw; do
  ffmpeg -y -v error -i "$WORK/src.mov" -c:v copy -c:a "$codec" -ar 8000 "$WORK/$codec.mov"
  "$BIN" analyze "$WORK/$codec.mov" --manifest "$WORK/src.mov.manifest.json" --json "$WORK/$codec.json" >/dev/null
  python3 - "$WORK/$codec.json" "$codec" <<'PY'
import json, sys
doc = json.load(open(sys.argv[1]))
med = doc["statistics"]["window"]["median"]
valid = doc["statistics"]["window"]["valid"]
assert valid >= 1, (sys.argv[2], valid)
assert abs(med) <= 5.0, (sys.argv[2], med)
print("ok", sys.argv[2], med, "valid", valid)
PY
done

python3 - "$WORK/a.json" "$WORK/b.json" <<'PY'
import json, sys
def doc(ns):
    return {
        "schema": "mxl-avsync-result/1",
        "mode": "LIVE",
        "reference": {"type": "tai-ptp", "sync_state": "unknown", "accuracy_ns": "unknown"},
        "events": [{"event_id": "EVT-000001", "video_arrival_time": ns, "video_event_time": ns}],
    }
json.dump(doc(1_000_000_000), open(sys.argv[1], "w"))
json.dump(doc(1_040_000_000), open(sys.argv[2], "w"))
PY
"$BIN" merge "$WORK/a.json" "$WORK/b.json" --json "$WORK/merge.json" >/dev/null
python3 - "$WORK/merge.json" <<'PY'
import json, sys
doc = json.load(open(sys.argv[1]))
ev = doc["events"][0]
assert ev["valid"] is True
assert abs(ev["video_e2e_latency_ms"] - 40.0) < 1e-6
print("ok cross-node merge", ev["video_e2e_latency_ms"])
PY

echo "stage 2 integration ok"
