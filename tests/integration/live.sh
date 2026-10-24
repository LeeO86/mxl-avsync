#!/usr/bin/env bash
# Live MXL checks (SPECIFICATION §14.2). This binary does not link libmxl, so the
# script reports that and exits 0. A host build that links libmxl should replace the
# body with the generator/analyzer domain test.
echo "live.sh: skipped. libmxl is not linked; /readyz stays 503 until a Linux host build creates MXL flows."
exit 0
