"""Writes calibrator.kicad_sch (and the custom MCU symbol calibrator.kicad_sym).

Drawn for people: power symbols at the top, GND at the bottom, signals left to right,
real wires inside each block, net labels only for the programming pads.
Parts, values, footprints and LCSC numbers come from design.py; check_netlist.py
compares the drawn connections with design.py.
Run inside the KiCad image (needs /usr/share/kicad/symbols):  python3 gen_sch.py
"""
import math
import os
import re
import uuid

import design as D

HERE = os.path.dirname(os.path.abspath(__file__))
PRJ = os.path.join(HERE, "..")
OUT = os.path.join(PRJ, "calibrator.kicad_sch")
SYMLIB = os.path.join(PRJ, "calibrator.kicad_sym")
SYMDIR = os.environ.get("KICAD_SYMBOL_DIR", "/usr/share/kicad/symbols")
ROOT = str(uuid.uuid5(uuid.NAMESPACE_URL, "calibrator-root"))
_n = [0]


def u():
    _n[0] += 1
    return str(uuid.uuid5(uuid.NAMESPACE_URL, f"calibrator-sch-{_n[0]}"))


# ---------------------------------------------------------------- custom MCU symbol
# Pins grouped by job: inputs left, outputs right, power top, ground bottom.
MCU_PINS = [  # (number, name, type, x, y, angle)
    ("14", "PA3/COMP2+", "bidirectional", -12.7, 10.16, 0),
    ("13", "PA2/COMP2-", "bidirectional", -12.7, 7.62, 0),
    ("2", "PA13/SWDIO", "bidirectional", -12.7, -2.54, 0),
    ("3", "PA14/SWCLK", "bidirectional", -12.7, -5.08, 0),
    ("10", "PF2/NRST", "bidirectional", -12.7, -7.62, 0),
    ("7", "PA5", "bidirectional", 12.7, 10.16, 180),
    ("6", "PA6", "bidirectional", 12.7, 7.62, 180),
    ("11", "PA0", "bidirectional", 12.7, 2.54, 180),
    ("12", "PA1", "bidirectional", 12.7, 0.0, 180),
    ("15", "PA4", "bidirectional", 12.7, -2.54, 180),
    ("16", "PA7", "bidirectional", 12.7, -5.08, 180),
    ("17", "PB0", "bidirectional", 12.7, -7.62, 180),
    ("19", "PB1", "bidirectional", 12.7, -10.16, 180),
    ("18", "PB2", "bidirectional", 12.7, -12.7, 180),
    ("4", "PB3", "bidirectional", 12.7, -15.24, 180),
    ("8", "PF0", "bidirectional", 12.7, -17.78, 180),
    ("9", "PF1", "bidirectional", 12.7, -20.32, 180),
    ("20", "VCC", "power_in", 0.0, 17.78, 270),
    ("1", "GND", "power_in", -2.54, -25.4, 90),
    ("5", "PB6/BOOT0", "passive", 2.54, -25.4, 90),
]


def mcu_symbol(name):
    s = (f'  (symbol "{name}" (pin_names (offset 1.016)) (exclude_from_sim no) (in_bom yes) (on_board yes)\n'
         f'    (property "Reference" "U" (at -10.16 16.51 0) (effects (font (size 1.27 1.27)) (justify left)))\n'
         f'    (property "Value" "PY32F002AF15P" (at 2.54 16.51 0) (effects (font (size 1.27 1.27)) (justify left)))\n'
         f'    (property "Footprint" "Package_SO:TSSOP-20_4.4x6.5mm_P0.65mm" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))\n'
         f'    (property "Datasheet" "https://www.puyasemi.com" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))\n'
         f'    (property "Description" "PUYA PY32F002A, Cortex-M0+, 20 KB flash, 3 KB RAM, TSSOP-20; pins grouped by use" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))\n'
         f'    (symbol "PY32F002AF15P_0_1" (rectangle (start -10.16 15.24) (end 10.16 -22.86) '
         f'(stroke (width 0.254) (type default)) (fill (type background))))\n'
         f'    (symbol "PY32F002AF15P_1_1"\n')
    for num, pname, typ, x, y, a in MCU_PINS:
        s += (f'      (pin {typ} line (at {x} {y} {a}) (length 2.54) (name "{pname}" (effects (font (size 1.27 1.27)))) '
              f'(number "{num}" (effects (font (size 1.27 1.27)))))\n')
    s += '    )\n    (embedded_fonts no)\n  )\n'
    return s


def write_symlib():
    with open(SYMLIB, "w") as f:
        f.write('(kicad_symbol_lib (version 20241209) (generator "calibrator-gen") (generator_version "9.0")\n')
        f.write(mcu_symbol("PY32F002AF15P"))
        f.write(')\n')


# ---------------------------------------------------------------- library helpers
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
    path = SYMLIB if lib == "calibrator" else os.path.join(SYMDIR, lib + ".kicad_sym")
    t = open(path).read()
    b = block(t, t.find(f'(symbol "{name}"'))
    if "(extends" in b:
        raise ValueError(f"{lib_id} uses extends; pick a base symbol")
    return b.replace(f'(symbol "{name}"', f'(symbol "{lib_id}"', 1)


def pins(sym):
    out = {}
    for m in re.finditer(r'\(pin \w+ \w+\s*\(at ([-\d.]+) ([-\d.]+) (\d+)\).*?\(number "([^"]*)"', sym, re.S):
        out.setdefault(m.group(4), (float(m.group(1)), float(m.group(2)), int(m.group(3))))
    return out


def to_sheet(sx, sy, rot, px, py):
    a = math.radians(rot)
    x = px * math.cos(a) - py * math.sin(a)
    y = px * math.sin(a) + py * math.cos(a)
    return round(sx + x, 2), round(sy - y, 2)


def eff(size=1.27, hide=False, justify=None, bold=False):
    j = f" (justify {justify})" if justify else ""
    h = " (hide yes)" if hide else ""
    b = " (bold yes)" if bold else ""
    return f"(effects (font (size {size} {size}){b}){j}{h})"


def prop(name, value, x, y, hide=False, justify=None, angle=0):
    return f'    (property "{name}" "{value}" (at {x} {y} {angle}) {eff(hide=hide, justify=justify)})\n'


# ---------------------------------------------------------------- drawing
# Schematic position of every part: (x, y, rotation). Grid 1.27 mm.
SCH = {
    # Power in
    "J1": (30.48, 71.12, 0), "C1": (58.42, 53.34, 0), "R3": (68.58, 45.72, 90),
    "C2": (78.74, 53.34, 0), "C3": (86.36, 53.34, 0), "R1": (66.04, 86.36, 0), "R2": (55.88, 86.36, 0),
    # Light sensors
    "Q2": (114.3, 55.88, 0), "R7": (127.0, 73.66, 0), "C4": (137.16, 73.66, 0),
    "Q3": (114.3, 88.9, 0), "R8": (127.0, 111.76, 0), "C5": (137.16, 111.76, 0),
    # MCU
    "U1": (180.34, 76.2, 0),
    # Flash + click
    "R4": (213.36, 66.04, 90), "D1": (228.6, 66.04, 180),
    "R5": (213.36, 104.14, 90), "R6": (220.98, 111.76, 0), "Q1": (228.6, 104.14, 0),
    "D2": (231.14, 88.9, 270), "BZ1": (243.84, 93.98, 0),
    # Programming
    "TP1": (27.94, 142.24, 0), "TP2": (40.64, 142.24, 0), "TP3": (53.34, 142.24, 0),
    "TP4": (66.04, 142.24, 0), "TP5": (78.74, 142.24, 0),
}

# Reference / value text position per part: (dx, dy, justify) twice.
FIELDS = {
    "J1": ((0, -17.78, None), (0, -15.24, None)),
    "U1": ((-10.16, 34.29, "left"), (-10.16, 36.83, "left")),
    "D1": ((0, -6.35, None), (0, -3.81, None)),
    "D2": ((-2.54, -1.27, "left"), (-2.54, 1.27, "left")),
    "Q1": ((5.08, -1.27, "left"), (5.08, 1.27, "left")),
    "Q2": ((5.08, -1.27, "left"), (5.08, 1.27, "left")),
    "Q3": ((5.08, -1.27, "left"), (5.08, 1.27, "left")),
    "BZ1": ((2.54, -5.08, "left"), (2.54, 5.08, "left")),
    "R3": ((0, -2.54, None), (0, 2.54, None)),
    "R4": ((0, -2.54, None), (0, 2.54, None)),
    "R5": ((0, -2.54, None), (0, 2.54, None)),
}
for _tp in ("TP1", "TP2", "TP3", "TP4", "TP5"):
    FIELDS[_tp] = ((0, -5.08, None), (0, -7.62, None))

# Wires (polylines). Every T is drawn as segments meeting at one point.
WIRES = [
    # --- Power in
    [(45.72, 63.5), (50.8, 63.5)], [(50.8, 63.5), (50.8, 45.72)],            # VBUS from J1
    [(50.8, 45.72), (58.42, 45.72)], [(58.42, 45.72), (64.77, 45.72)],       # VBUS rail
    [(58.42, 45.72), (58.42, 49.53)],                                        # C1
    [(72.39, 45.72), (78.74, 45.72)], [(78.74, 45.72), (86.36, 45.72)],      # VDD rail
    [(86.36, 45.72), (91.44, 45.72)],
    [(78.74, 45.72), (78.74, 49.53)], [(86.36, 45.72), (86.36, 49.53)],      # C2, C3
    [(45.72, 76.2), (66.04, 76.2), (66.04, 82.55)],                          # CC1 -> R1
    [(45.72, 78.74), (55.88, 78.74), (55.88, 82.55)],                        # CC2 -> R2
    [(30.48, 88.9), (30.48, 93.98)], [(22.86, 88.9), (22.86, 93.98), (30.48, 93.98)],  # GND + shield
    [(58.42, 57.15), (58.42, 59.69)], [(78.74, 57.15), (78.74, 59.69)], [(86.36, 57.15), (86.36, 59.69)],
    [(66.04, 90.17), (66.04, 92.71)], [(55.88, 90.17), (55.88, 92.71)],
    # --- Light sensors
    [(116.84, 50.8), (116.84, 48.26)], [(116.84, 83.82), (116.84, 81.28)],  # collectors to VDD
    [(116.84, 60.96), (116.84, 66.04)], [(116.84, 66.04), (127.0, 66.04)],   # SENS_A
    [(127.0, 66.04), (137.16, 66.04)], [(137.16, 66.04), (167.64, 66.04)],
    [(127.0, 66.04), (127.0, 69.85)], [(137.16, 66.04), (137.16, 69.85)],
    [(127.0, 77.47), (127.0, 80.01)], [(137.16, 77.47), (137.16, 80.01)],
    [(116.84, 93.98), (116.84, 104.14)], [(116.84, 104.14), (127.0, 104.14)],  # SENS_B
    [(127.0, 104.14), (137.16, 104.14)], [(137.16, 104.14), (152.4, 104.14)],
    [(152.4, 104.14), (152.4, 68.58), (167.64, 68.58)],
    [(127.0, 104.14), (127.0, 107.95)], [(137.16, 104.14), (137.16, 107.95)],
    [(127.0, 115.57), (127.0, 118.11)], [(137.16, 115.57), (137.16, 118.11)],
    # --- MCU
    [(180.34, 58.42), (180.34, 55.88)],
    [(177.8, 101.6), (177.8, 104.14)], [(182.88, 101.6), (182.88, 104.14), (177.8, 104.14)],
    [(167.64, 78.74), (162.56, 78.74)], [(167.64, 81.28), (162.56, 81.28)], [(167.64, 83.82), (162.56, 83.82)],
    # --- Flash + click
    [(193.04, 66.04), (209.55, 66.04)], [(217.17, 66.04), (224.79, 66.04)],   # PA5 -> R4 -> LED
    [(232.41, 66.04), (237.49, 66.04), (237.49, 68.58)],                     # LED cathode -> GND
    [(193.04, 68.58), (198.12, 68.58), (198.12, 104.14), (209.55, 104.14)],   # PA6 -> R5
    [(217.17, 104.14), (220.98, 104.14)], [(220.98, 104.14), (223.52, 104.14)],
    [(220.98, 104.14), (220.98, 107.95)], [(220.98, 115.57), (220.98, 118.11)],
    [(231.14, 109.22), (231.14, 111.76)],
    [(231.14, 99.06), (231.14, 96.52)], [(231.14, 96.52), (231.14, 92.71)],   # BUZ_N node
    [(231.14, 96.52), (241.3, 96.52)],
    [(231.14, 85.09), (231.14, 81.28)], [(231.14, 81.28), (236.22, 81.28)],      # VBUS to D2 and buzzer
    [(236.22, 81.28), (236.22, 91.44), (241.3, 91.44)],
    # --- Programming
    [(27.94, 142.24), (27.94, 147.32)], [(40.64, 142.24), (40.64, 147.32)],
    [(53.34, 142.24), (53.34, 147.32)], [(66.04, 142.24), (66.04, 147.32)], [(78.74, 142.24), (78.74, 147.32)],
]

JUNCTIONS = [(58.42, 45.72), (78.74, 45.72), (86.36, 45.72), (127.0, 66.04), (137.16, 66.04),
             (127.0, 104.14), (137.16, 104.14), (220.98, 104.14), (231.14, 96.52)]

POWER = [  # (lib_id, x, y, rotation)
    ("power:VBUS", 50.8, 45.72, 0), ("power:VDD", 91.44, 45.72, 0),
    ("power:GND", 30.48, 93.98, 0), ("power:GND", 58.42, 59.69, 0), ("power:GND", 78.74, 59.69, 0),
    ("power:GND", 86.36, 59.69, 0), ("power:GND", 66.04, 92.71, 0), ("power:GND", 55.88, 92.71, 0),
    ("power:VDD", 116.84, 48.26, 0), ("power:VDD", 116.84, 81.28, 0),
    ("power:GND", 127.0, 80.01, 0), ("power:GND", 137.16, 80.01, 0),
    ("power:GND", 127.0, 118.11, 0), ("power:GND", 137.16, 118.11, 0),
    ("power:VDD", 180.34, 55.88, 0), ("power:GND", 177.8, 104.14, 0),
    ("power:GND", 237.49, 68.58, 0), ("power:GND", 220.98, 118.11, 0), ("power:GND", 231.14, 111.76, 0),
    ("power:VBUS", 231.14, 81.28, 0),
    ("power:VDD", 27.94, 147.32, 180), ("power:GND", 40.64, 147.32, 0),
]
FLAGS = [(50.8, 63.5), (22.86, 93.98), (78.74, 45.72)]  # VBUS, GND, VDD

LABELS = [  # (net, x, y, angle)  only for the programming pads
    ("SWDIO", 162.56, 78.74, 180), ("SWCLK", 162.56, 81.28, 180), ("NRST", 162.56, 83.82, 180),
    ("SWDIO", 53.34, 147.32, 270), ("SWCLK", 66.04, 147.32, 270), ("NRST", 78.74, 147.32, 270),
]

BLOCKS = [  # (title, x1, y1, x2, y2)
    ("Power in (USB-C, 5 V from the phone)", 15.24, 33.02, 99.06, 101.6),
    ("Light sensors (display mode)", 101.6, 33.02, 157.48, 124.46),
    ("MCU", 160.02, 33.02, 195.58, 124.46),
    ("Flash + click (camera and display mode)", 196.85, 33.02, 264.16, 124.46),
    ("Programming pads (bottom side, pogo jig)", 15.24, 127.0, 99.06, 160.02),
]

NOTES = [
    (17.78, 104.14, "5.1k on CC1/CC2 asks the phone for 5 V. D+/D- are not connected."),
    (17.78, 106.68, "PY32 runs straight from 5 V (1.7-5.5 V). R3 + C2/C3 filter spikes; C2/C3 sit at U1."),
    (104.14, 127.0, "Reverse-gullwing parts look down through holes."),
    (104.14, 129.54, "COMP2 flips where A and B see the same light;"),
    (104.14, 132.08, "the ADC reads the same pins (dark / light)."),
    (199.39, 127.0, "Reverse-mount white LED shines through a hole (~12 mA)."),
    (199.39, 129.54, "PA5 and PA6 are set by one register write."),
    (199.39, 132.08, "D2 catches the coil kick; R6 keeps Q1 off during reset."),
    (162.56, 127.0, "BOOT0 tied low:"),
    (162.56, 129.54, "boot from flash."),
]


def symbol_instance(lib_id, ref, value, fp, lcsc, x, y, rot, sym, in_bom=True, hide_ref=False, fields=None):
    s = (f'  (symbol (lib_id "{lib_id}") (at {x} {y} {rot}) (unit 1) (exclude_from_sim no) '
         f'(in_bom {"yes" if in_bom else "no"}) (on_board yes) (dnp no) (uuid "{u()}")\n')
    (rx, ry, rj), (vx, vy, vj) = fields or ((2.54, -1.27, "left"), (2.54, 1.27, "left"))
    fa = rot if rot in (90, 270) else 0  # keep text horizontal on turned parts
    s += prop("Reference", ref, x + rx, y + ry, hide=hide_ref, justify=rj, angle=fa)
    s += prop("Value", value, x + vx, y + vy, justify=vj, angle=fa)
    s += prop("Footprint", fp, x, y, hide=True)
    s += prop("Datasheet", "~", x, y, hide=True)
    s += prop("Description", "", x, y, hide=True)
    if lcsc:
        s += prop("LCSC", lcsc, x, y, hide=True)
    for num in pins(sym):
        s += f'    (pin "{num}" (uuid "{u()}"))\n'
    s += f'    (instances (project "calibrator" (path "/{ROOT}" (reference "{ref}") (unit 1))))\n  )\n'
    return s


def main():
    write_symlib()
    libs = {}
    for ref, p in D.PARTS.items():
        libs.setdefault(p[0], lib_symbol(p[0]))
    for lib_id in ("power:PWR_FLAG", "power:GND", "power:VDD", "power:VBUS"):
        libs[lib_id] = lib_symbol(lib_id)

    body = ""
    nc = ""
    for ref, (lib_id, value, fp, lcsc, netmap, _pcb) in D.PARTS.items():
        x, y, rot = SCH[ref]
        sym = libs[lib_id]
        body += symbol_instance(lib_id, ref, value, fp, lcsc, x, y, rot, sym,
                                in_bom=not ref.startswith("TP"), fields=FIELDS.get(ref, None) or (
                                    ((2.54, -1.27, "left"), (2.54, 1.27, "left")) if lib_id in ("Device:R", "Device:C")
                                    else None))
        for num, (px, py, _a) in pins(sym).items():
            if netmap.get(num, "x") is None:
                nx, ny = to_sheet(x, y, rot, px, py)
                nc += f'  (no_connect (at {nx} {ny}) (uuid "{u()}"))\n'

    for i, (lib_id, x, y, rot) in enumerate(POWER):
        name = lib_id.split(":")[1]
        body += symbol_instance(lib_id, f"#PWR0{i + 1:02d}", name, "", "", x, y, rot, libs[lib_id],
                                hide_ref=True, fields=((0, 3.81 if name == "GND" else -3.81, None),
                                                       (0, 3.81 if name == "GND" else -3.81, None)) if rot == 0 else
                                ((0, 3.81, None), (0, 3.81, None)))
    for i, (x, y) in enumerate(FLAGS):
        body += symbol_instance("power:PWR_FLAG", f"#FLG0{i + 1}", "PWR_FLAG", "", "", x, y, 0,
                                libs["power:PWR_FLAG"], hide_ref=True, fields=((0, -3.81, None), (0, -3.81, None)))

    wires = ""
    for poly in WIRES:
        for a, b in zip(poly, poly[1:]):
            wires += (f'  (wire (pts (xy {a[0]} {a[1]}) (xy {b[0]} {b[1]})) (stroke (width 0) (type default)) '
                      f'(uuid "{u()}"))\n')
    for x, y in JUNCTIONS:
        wires += f'  (junction (at {x} {y}) (diameter 0) (color 0 0 0 0) (uuid "{u()}"))\n'
    for net, x, y, a in LABELS:
        just = "right bottom" if a in (180, 270) else "left bottom"
        wires += f'  (label "{net}" (at {x} {y} {a}) {eff(justify=just)} (uuid "{u()}"))\n'

    art = ""
    for title, x1, y1, x2, y2 in BLOCKS:
        art += (f'  (rectangle (start {x1} {y1}) (end {x2} {y2}) (stroke (width 0.254) (type dash) '
                f'(color 72 110 150 1)) (fill (type none)) (uuid "{u()}"))\n')
        art += (f'  (text "{title}" (exclude_from_sim no) (at {x1 + 1.27} {y1 + 3.81} 0) '
                f'{eff(size=1.8, bold=True, justify="left bottom")} (uuid "{u()}"))\n')
    for x, y, s in NOTES:
        art += f'  (text "{s}" (exclude_from_sim no) (at {x} {y} 0) {eff(justify="left bottom")} (uuid "{u()}"))\n'

    out = (
        f'(kicad_sch (version 20250114) (generator "eeschema") (generator_version "9.0") (uuid "{ROOT}") (paper "A4")\n'
        '  (title_block (title "AVsync calibrator") (date "2026-11-08") (rev "0.2")\n'
        '    (comment 1 "v1 proposal - not built or tested yet")\n'
        '    (comment 2 "Generated by scripts/gen_sch.py from scripts/design.py")\n'
        '    (comment 3 "SPDX-License-Identifier: CERN-OHL-P-2.0"))\n'
        '  (lib_symbols\n' + "".join(libs.values()) + '\n  )\n'
        + art + wires + nc + body +
        '  (sheet_instances (path "/" (page "1")))\n  (embedded_fonts no)\n)\n'
    )
    with open(OUT, "w") as f:
        f.write(out)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
