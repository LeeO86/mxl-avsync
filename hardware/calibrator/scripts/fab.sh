#!/bin/sh
# Export JLCPCB files: gerbers + drill (zipped), BOM and CPL (pick and place),
# 3D renders and the schematic as PDF/SVG. Needs docker (pinned KiCad image).
set -e
cd "$(dirname "$0")/.."
mkdir -p fab/gerbers renders
rm -f fab/gerbers/* fab/calibrator-gerbers.zip
scripts/kicad.sh sh -c '
kicad-cli pcb export gerbers --layers F.Cu,B.Cu,F.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts \
  --subtract-soldermask -o fab/gerbers/ calibrator.kicad_pcb &&
kicad-cli pcb export drill --format excellon --excellon-units mm --excellon-oval-format alternate \
  --excellon-separate-th -o fab/gerbers/ calibrator.kicad_pcb &&
kicad-cli pcb export pos --side front --format csv --units mm --exclude-dnp -o fab/calibrator-pos.csv calibrator.kicad_pcb &&
kicad-cli sch export pdf -o renders/calibrator-schematic.pdf calibrator.kicad_sch &&
kicad-cli sch export svg -o renders/ calibrator.kicad_sch &&
kicad-cli pcb render --side top --width 1600 --height 700 --background opaque --quality high --zoom 1.8 -o renders/calibrator-top.png calibrator.kicad_pcb &&
kicad-cli pcb render --side bottom --width 1600 --height 700 --background opaque --quality high --zoom 1.8 -o renders/calibrator-bottom.png calibrator.kicad_pcb &&
kicad-cli pcb render --side top --width 1600 --height 1000 --background opaque --quality high --perspective --rotate "-45,0,25" --zoom 1.3 -o renders/calibrator-3d.png calibrator.kicad_pcb
' > /dev/null
python3 scripts/jlc_files.py
python3 -c '
import os, zipfile
d = "fab/gerbers"
with zipfile.ZipFile("fab/calibrator-gerbers.zip", "w", zipfile.ZIP_DEFLATED) as z:
    for n in sorted(os.listdir(d)):
        z.write(os.path.join(d, n), n)
'
echo "fab files:"; ls fab
