"""Writes calibrator.kicad_sch from design.py.

Every pin gets a short wire and a net label, so the schematic shows the same
nets as the board. Symbol drawings are copied from the KiCad 9 libraries.
Run inside the KiCad image (needs /usr/share/kicad/symbols):
  python3 gen_sch.py
"""
import math
import os
import re
import uuid

import design as D

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "calibrator.kicad_sch")
SYMDIR = os.environ.get("KICAD_SYMBOL_DIR", "/usr/share/kicad/symbols")
ROOT = str(uuid.uuid5(uuid.NAMESPACE_URL, "calibrator-root"))
_n = [0]


def u():
    _n[0] += 1
    return str(uuid.uuid5(uuid.NAMESPACE_URL, f"calibrator-sch-{_n[0]}"))


def block(text, start):
    d = 0
    for j in range(start, len(text)):
        if text[j] == "(":
            d += 1
        elif text[j] == ")":
            d -= 1
            if d == 0:
                return text[start:j + 1]
    raise ValueError("unbalanced")


def lib_symbol(lib_id):
    lib, name = lib_id.split(":")
    t = open(os.path.join(SYMDIR, lib + ".kicad_sym")).read()
    i = t.find(f'(symbol "{name}"')
    b = block(t, i)
    if "(extends" in b:
        raise ValueError(f"{lib_id} uses extends; pick a base symbol")
    return b.replace(f'(symbol "{name}"', f'(symbol "{lib_id}"', 1)


def pins(sym):
    """pin number -> (x, y, angle) in symbol coordinates (y up)."""
    out = {}
    for m in re.finditer(r'\(pin \w+ \w+\s*\(at ([-\d.]+) ([-\d.]+) (\d+)\).*?\(number "([^"]*)"', sym, re.S):
        out.setdefault(m.group(4), (float(m.group(1)), float(m.group(2)), int(m.group(3))))
    return out


def to_sheet(sx, sy, rot, px, py):
    """Symbol pin position -> sheet position. Sheet y is down, symbol y is up."""
    a = math.radians(rot)
    x = px * math.cos(a) - py * math.sin(a)
    y = px * math.sin(a) + py * math.cos(a)
    return round(sx + x, 2), round(sy - y, 2)


def stub_dir(angle, rot):
    """Unit vector (sheet coords) pointing away from the symbol body."""
    a = math.radians(angle + 180 + rot)
    return round(math.cos(a)), -round(math.sin(a))


def eff(size=1.27, hide=False, justify=None):
    j = f" (justify {justify})" if justify else ""
    h = " (hide yes)" if hide else ""
    return f"(effects (font (size {size} {size})){j}{h})"


def prop(name, value, x, y, hide=False, angle=0, justify=None):
    return f'    (property "{name}" "{value}" (at {x} {y} {angle}) {eff(hide=hide, justify=justify)})\n'


# Where Reference and Value sit, relative to the symbol (dx, dy, justify), per symbol type.
FIELDS = {
    "MCU_Puya:PY32F002AF15P": ((8.89, -26.67, "left"), (8.89, 26.67, "left")),
    "Connector:USB_C_Receptacle_PowerOnly_6P": ((0, -21.59, None), (0, 27.94, None)),
    "Device:LED": ((0, -3.81, None), (0, 3.81, None)),
    "Device:D": ((0, -3.81, None), (0, 3.81, None)),
    "Device:Q_Photo_NPN": ((5.08, -1.27, "left"), (5.08, 1.27, "left")),
    "Transistor_BJT:Q_NPN_BEC": ((5.08, -1.27, "left"), (5.08, 1.27, "left")),
    "Device:Buzzer": ((2.54, -5.08, "left"), (2.54, 5.08, "left")),
    "Connector:TestPoint": ((0, -5.08, None), (1.27, -2.54, "left")),
}


def main():
    libs = {}
    for ref, p in D.PARTS.items():
        libs.setdefault(p[0], lib_symbol(p[0]))
    libs["power:PWR_FLAG"] = lib_symbol("power:PWR_FLAG")

    body = ""
    wires = ""
    for ref, (lib_id, value, fp, lcsc, netmap, (sx, sy, rot), _pcb) in D.PARTS.items():
        sym = libs[lib_id]
        is_tp = lib_id == "Connector:TestPoint"
        body += (f'  (symbol (lib_id "{lib_id}") (at {sx} {sy} {rot}) (unit 1) (exclude_from_sim no) '
                 f'(in_bom {"no" if is_tp else "yes"}) (on_board yes) (dnp no) (uuid "{u()}")\n')
        (rx, ry, rj), (vx, vy, vj) = FIELDS.get(lib_id, ((2.54, -1.27, "left"), (2.54, 1.27, "left")))
        body += prop("Reference", ref, sx + rx, sy + ry, justify=rj)
        body += prop("Value", value, sx + vx, sy + vy, justify=vj)
        body += prop("Footprint", fp, sx, sy, hide=True)
        body += prop("Datasheet", "~", sx, sy, hide=True)
        body += prop("Description", "", sx, sy, hide=True)
        if lcsc:
            body += prop("LCSC", lcsc, sx, sy, hide=True)
        for num in pins(sym):
            body += f'    (pin "{num}" (uuid "{u()}"))\n'
        body += (f'    (instances (project "calibrator" (path "/{ROOT}" (reference "{ref}") (unit 1))))\n  )\n')
        placed = set()
        for num, (px, py, ang) in pins(sym).items():
            x, y = to_sheet(sx, sy, rot, px, py)
            if (x, y) in placed:
                continue  # stacked pins (USB-C GND/VBUS) share one label
            placed.add((x, y))
            net = netmap.get(num)
            if net is None:
                wires += f'  (no_connect (at {x} {y}) (uuid "{u()}"))\n'
                continue
            dx, dy = stub_dir(ang, rot)
            x2, y2 = round(x + 2.54 * dx, 2), round(y + 2.54 * dy, 2)
            wires += f'  (wire (pts (xy {x} {y}) (xy {x2} {y2})) (stroke (width 0) (type default)) (uuid "{u()}"))\n'
            la = {(1, 0): 0, (-1, 0): 180, (0, -1): 90, (0, 1): 270}[(dx, dy)]
            just = "left bottom" if la in (0, 90) else "right bottom"
            wires += f'  (label "{net}" (at {x2} {y2} {la}) {eff(justify=just)} (uuid "{u()}"))\n'

    for i, (net, (x, y)) in enumerate(D.PWR_FLAGS.items()):
        body += (f'  (symbol (lib_id "power:PWR_FLAG") (at {x} {y} 0) (unit 1) (exclude_from_sim no) '
                 f'(in_bom yes) (on_board yes) (dnp no) (uuid "{u()}")\n')
        body += prop("Reference", f"#FLG0{i + 1}", x, y - 3.81, hide=True)
        body += prop("Value", "PWR_FLAG", x, y - 3.81)
        body += prop("Footprint", "", x, y, hide=True)
        body += prop("Datasheet", "~", x, y, hide=True)
        body += prop("Description", "", x, y, hide=True)
        body += f'    (pin "1" (uuid "{u()}"))\n'
        body += (f'    (instances (project "calibrator" (path "/{ROOT}" (reference "#FLG0{i + 1}") (unit 1))))\n  )\n')
        wires += f'  (wire (pts (xy {x} {y}) (xy {x} {y + 2.54})) (stroke (width 0) (type default)) (uuid "{u()}"))\n'
        wires += f'  (label "{net}" (at {x} {y + 2.54} 270) {eff(justify="right bottom")} (uuid "{u()}"))\n'

    notes = [
        (20.32, 30.48, "USB-C power only: the phone supplies 5 V because of the 5.1k pull-downs on CC1/CC2. D+/D- are not on this receptacle."),
        (20.32, 104.14, "Click: S8050 switches the passive buzzer from VBUS. D2 catches the coil kick."),
        (20.32, 106.68, "R6 keeps the buzzer off while the MCU resets."),
        (172.72, 33.02, "White reverse-mount LED shines through a hole in the board (camera mode). About 12 mA from PA5."),
        (180.34, 147.32, "Two reverse-gullwing phototransistors look through holes in the board (display mode)."),
        (180.34, 149.86, "COMP2 compares SENS_A (PA3, +) with SENS_B (PA2, -). The ADC reads the same pins for dark/light."),
        (91.44, 172.72, "Programming pads on the flat bottom side: VMCU, GND, SWDIO, SWCLK, NRST (pogo jig)."),
        (20.32, 33.02, "PY32 runs from 5 V directly (VCC 1.7-5.5 V, abs max 6.25 V). R3/C2 filter VBUS spikes."),
    ]
    text = ""
    for x, y, s in notes:
        text += f'  (text "{s}" (exclude_from_sim no) (at {x} {y} 0) {eff(justify="left bottom")} (uuid "{u()}"))\n'

    out = (
        f'(kicad_sch (version 20250114) (generator "eeschema") (generator_version "9.0") (uuid "{ROOT}") (paper "A4")\n'
        '  (title_block (title "AVsync calibrator") (date "2026-11-08") (rev "0.1")\n'
        '    (comment 1 "v1 proposal - not built or tested yet")\n'
        '    (comment 2 "Generated by scripts/gen_sch.py from scripts/design.py")\n'
        '    (comment 3 "License: MIT (see repository LICENSE)"))\n'
        '  (lib_symbols\n' + "".join(libs.values()) + '\n  )\n'
        + text + wires + body +
        '  (sheet_instances (path "/" (page "1")))\n  (embedded_fonts no)\n)\n'
    )
    with open(OUT, "w") as f:
        f.write(out)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
