#!/usr/bin/env bash
# The image liveness check is /livez. This does not build the image.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
grep -q '^HEALTHCHECK' "$ROOT/docker/Dockerfile"
grep -q '/livez' "$ROOT/docker/Dockerfile"
grep -q 'WEB_PORT' "$ROOT/docker/Dockerfile"
echo "healthcheck.sh ok"
