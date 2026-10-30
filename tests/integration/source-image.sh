#!/usr/bin/env bash
# Live generator draws the uploaded source picture into its MXL video flow, replaces it on a new
# upload and draws the stored picture again after a restart. Needs libmxl (build-and-test job).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${BIN:-$ROOT/build/mxl-avsync}"
SAMPLE="${SAMPLE:-$ROOT/build/avsync-sample-flow}"
export LD_LIBRARY_PATH="${MXL_LIB_DIR:-/opt/mxl/lib}${VCPKG_LIB_DIR:+:$VCPKG_LIB_DIR}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
[[ -x "$BIN" && -x "$SAMPLE" ]] || { echo "missing $BIN or $SAMPLE" >&2; exit 1; }

WORK="$(mktemp -d /dev/shm/avs-img-XXXXXX)"
PID=""
stop_gen() { [[ -n "$PID" ]] && { kill -TERM "$PID" 2>/dev/null || true; wait "$PID" 2>/dev/null || true; }; PID=""; }
cleanup() { stop_gen; rm -rf "$WORK"; }
trap cleanup EXIT
PORT=18860
API="http://127.0.0.1:$PORT"

start_gen() {
  AVSYNC_ROLE=generator AVSYNC_VIDEO_FORMAT=1080p50 AVSYNC_SOURCE="Source picture" \
    AVSYNC_STATE_DIR="$WORK/state" NMOS_ENABLE=false NMOS_SEED=img-gen \
    MXL_DOMAIN_SCAN_PATH="$WORK" MXL_OUTPUT_DOMAIN_DIR="$WORK/gen" MXL_CLEANUP_ON_EXIT=true \
    WEB_PORT="$PORT" NMOS_PORT=18862 \
    "$BIN" serve >>"$WORK/gen.log" 2>&1 &
  PID=$!
  for _ in $(seq 1 100); do
    [[ "$(curl -s -o /dev/null -w '%{http_code}' "$API/readyz" || true)" == "200" ]] && return 0
    sleep 0.1
  done
  cat "$WORK/gen.log" >&2
  echo "generator not ready" >&2
  return 1
}

# solid <r> <g> <b> <file>: opaque 64x64 PNG of one colour (standard library only).
solid() {
  python3 - "$@" <<'PY'
import struct, sys, zlib
r, g, b, out = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
raw = b''.join(b'\x00' + bytes((r, g, b, 255)) * 64 for _ in range(64))
chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
open(out, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 64, 64, 8, 6, 0, 0, 0))
                      + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))
PY
}

# count <colour>: pixels of the given saturated colour in the caption band (below the sync scale)
# of the newest grain. The bars there have no saturated red or blue, so only the source picture can
# supply them; the moving scale cursor above changes colour and is excluded.
count() {
  local flow
  flow=$(curl -sf "$API/api/v1/status" | python3 -c 'import json,sys; print(json.load(sys.stdin)["video_flow"])')
  sleep 0.3
  "$SAMPLE" "$WORK/gen" "$flow" "$WORK/grain.v210" >/dev/null
  python3 - "$WORK/grain.v210" "$1" <<'PY'
import struct, sys
data, colour = open(sys.argv[1], 'rb').read(), sys.argv[2]
width, height = 1920, 1080
stride = (width + 47) // 48 * 128
n = 0
for y in range(height * 13 // 16, height):
    words = struct.unpack_from('<%dI' % (stride // 4), data, y * stride)
    s = []
    for w in words:
        s += (w & 1023, (w >> 10) & 1023, (w >> 20) & 1023)
    for x in range(0, width, 2):
        cb, cr = s[2 * x], s[2 * x + 2]
        if (colour == 'red' and cr > 880 and cb < 480) or (colour == 'blue' and cb > 880 and cr < 600):
            n += 1
print(n)
PY
}

# upload <file> [expected http code]
upload() {
  local code
  code=$(curl -s -o "$WORK/upload.json" -w '%{http_code}' -X POST -H 'Content-Type: image/png' --data-binary @"$1" "$API/api/v1/generator/source-image")
  [[ "$code" == "${2:-200}" ]] || { cat "$WORK/upload.json" >&2; echo "upload $1: $code, expected ${2:-200}" >&2; exit 1; }
}

# gif <width> <height> <frames> <file>: tiny file, many full-canvas frames (1x1 pixel changes).
gif() {
  python3 - "$@" <<'PY'
import struct, sys
w, h, n, out = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
b = bytearray(b'GIF89a' + struct.pack('<HHBBB', w, h, 0, 0, 0))
b += b'\x21\xff\x0bNETSCAPE2.0\x03\x01\x00\x00\x00'
for i in range(n):
    b += b'\x21\xf9\x04\x00' + struct.pack('<H', 10) + b'\x00\x00'
    b += b'\x2c' + struct.pack('<HHHHB', i % w, 0, 1, 1, 0x80) + bytes((255, 0, 0, 0, 0, 255))
    b += b'\x02\x02\x44\x01\x00'
b += b'\x3b'
open(out, 'wb').write(b)
PY
}

rss_mib() { awk '/^VmRSS/ {print int($2 / 1024)}' "/proc/$PID/status"; }
alive() { kill -0 "$PID" 2>/dev/null && [[ "$(curl -s -o /dev/null -w '%{http_code}' "$API/readyz" || true)" == "200" ]]; }

mkdir -p "$WORK/state"
solid 255 0 0 "$WORK/red.png"
solid 0 0 255 "$WORK/blue.png"
start_gen
before=$(count red)
[[ "$before" -lt 50 ]] || { echo "red pixels before any upload: $before" >&2; exit 1; }
upload "$WORK/red.png"
red=$(count red)
[[ "$red" -ge 500 ]] || { echo "uploaded picture not drawn: $red red pixels" >&2; exit 1; }
upload "$WORK/blue.png"
blue=$(count blue); red_after=$(count red)
[[ "$blue" -ge 500 && "$red_after" -lt 50 ]] || { echo "new upload not drawn: blue $blue red $red_after" >&2; exit 1; }
stop_gen
start_gen
restored=$(count blue)
[[ "$restored" -ge 500 ]] || { echo "stored picture not drawn after restart: $restored" >&2; exit 1; }

# Small files that expand to many full-canvas frames are scaled at load and capped in decoded bytes.
gif 1920 1080 60 "$WORK/many.gif"
upload "$WORK/many.gif"
frames=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["frames"])' "$WORK/upload.json")
[[ "$frames" == "60" ]] || { echo "60-frame GIF reported $frames frames" >&2; exit 1; }
alive || { echo "generator died after a 60-frame 1080p GIF" >&2; exit 1; }
rss=$(rss_mib)
[[ "$rss" -lt 300 ]] || { echo "60-frame 1080p GIF: RSS $rss MiB" >&2; exit 1; }
gif 4096 4096 600 "$WORK/bomb.gif"
upload "$WORK/bomb.gif" 413
alive || { echo "generator died after an oversized GIF" >&2; exit 1; }
rss_bomb=$(rss_mib)
[[ "$rss_bomb" -lt 400 ]] || { echo "oversized GIF: RSS $rss_bomb MiB" >&2; exit 1; }
# A refused upload keeps the previous picture: same bytes served, same picture drawn.
cmp -s <(curl -sf "$API/api/v1/generator/source-image") "$WORK/many.gif" || { echo "refused upload replaced the stored picture" >&2; exit 1; }
[[ "$(curl -sf "$API/api/v1/generator" | python3 -c 'import json,sys; print(json.load(sys.stdin)["source_image_frames"])')" == "60" ]] \
  || { echo "refused upload changed the frame count" >&2; exit 1; }
# A broken stored picture is skipped with a warning at start; the generator still becomes ready.
stop_gen
printf 'GIF89a broken' > "$WORK/state/source-ident.gif"
start_gen
grep -q source_image_skipped "$WORK/gen.log" || { echo "no warning for a broken stored picture" >&2; exit 1; }
echo "source-image.sh ok (red $before -> $red, blue $blue, after restart $restored, 60-frame GIF RSS $rss MiB, oversized GIF 413 at RSS $rss_bomb MiB)"
