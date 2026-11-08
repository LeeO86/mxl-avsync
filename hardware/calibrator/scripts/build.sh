#!/bin/sh
# Rebuild footprints, schematic, placed board, autorouted board, ERC and DRC.
# Needs docker; KiCad 9.0.9 and Freerouting 2.1.0 images are pinned here.
set -e
cd "$(dirname "$0")/.."
python3 scripts/gen_footprints.py
scripts/kicad.sh sh -c 'cd scripts && python3 gen_sch.py && python3 gen_pcb.py place'
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD:/work" -w /work --entrypoint java \
  ghcr.io/freerouting/freerouting:2.1.0 -jar /app/freerouting-executable.jar \
  -de /work/calibrator.dsn -do /work/calibrator.ses -mp 30 --gui.enabled=false > /dev/null
scripts/kicad.sh sh -c 'cd scripts && python3 gen_pcb.py finish'
rm -f calibrator.dsn calibrator.ses
scripts/kicad.sh kicad-cli sch erc --severity-all -o /work/erc.rpt calibrator.kicad_sch
scripts/kicad.sh kicad-cli pcb drc --severity-all -o /work/drc.rpt calibrator.kicad_pcb
