"""Writes the JLCPCB BOM and CPL files from design.py and the KiCad position file.

fab/calibrator-bom-jlc.csv : Comment, Designator, Footprint, LCSC Part #
fab/calibrator-cpl-jlc.csv : Designator, Mid X, Mid Y, Layer, Rotation
Rotations are KiCad's. Check every part in JLC's placement preview before ordering
(or pay for "Confirm Parts Placement"); the custom footprints have not been checked by JLC.
"""
import csv
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design as D  # noqa: E402

FAB = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "fab")


def main():
    groups = {}
    for ref, p in D.PARTS.items():
        lcsc = p[3]
        if not lcsc:
            continue  # test pads are bare copper, nothing to assemble
        key = (p[1], p[2].split(":")[1], lcsc)
        groups.setdefault(key, []).append(ref)
    with open(os.path.join(FAB, "calibrator-bom-jlc.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
        for (value, fp, lcsc), refs in sorted(groups.items(), key=lambda kv: kv[1][0]):
            w.writerow([value, ",".join(sorted(refs)), fp, lcsc])

    rows = list(csv.DictReader(open(os.path.join(FAB, "calibrator-pos.csv"))))
    with open(os.path.join(FAB, "calibrator-cpl-jlc.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for r in rows:
            if not D.PARTS.get(r["Ref"], ("",) * 4)[3]:
                continue
            side = "Top" if r["Side"].lower() == "top" else "Bottom"
            rot = float(r["Rot"]) % 360
            w.writerow([r["Ref"], f'{float(r["PosX"]):.4f}mm', f'{float(r["PosY"]):.4f}mm', side, f"{rot:g}"])
    print("wrote BOM and CPL")


if __name__ == "__main__":
    main()
