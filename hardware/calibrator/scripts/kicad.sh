#!/bin/sh
# Run a command inside the pinned KiCad image with this folder mounted at /work.
# Usage: scripts/kicad.sh <command...>
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
exec docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$HERE:/work" -w /work kicad/kicad:9.0.9-full \
  sh -c 'mkdir -p "$HOME/.config/kicad/9.0" && cp /usr/share/kicad/template/sym-lib-table /usr/share/kicad/template/fp-lib-table "$HOME/.config/kicad/9.0/" && exec "$@"' sh "$@"
