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
    ("2", "PC16/UDM", "bidirectional", -17.78, 12.7, 0),
    ("3", "PC17/UDP/BOOT", "bidirectional", -17.78, 10.16, 0),
    ("5", "PA13/CMP3+", "bidirectional", -17.78, 2.54, 0),
    ("9", "PA0/ADC0", "bidirectional", -17.78, 0.0, 0),
    ("11", "PA2/CMP3-/ADC2", "bidirectional", -17.78, -2.54, 0),
    ("1", "PC19/SWCLK", "bidirectional", -17.78, -7.62, 0),
    ("28", "PC18/SWDIO", "bidirectional", -17.78, -10.16, 0),
    ("4", "PC14/PA12", "bidirectional", -17.78, -12.7, 0),
    ("8", "PC3", "bidirectional", -17.78, -15.24, 0),
    ("10", "PA1", "bidirectional", -17.78, -17.78, 0),
    ("12", "PA3", "bidirectional", -17.78, -20.32, 0),
    ("13", "PA6", "bidirectional", -17.78, -22.86, 0),
    ("14", "PB0", "bidirectional", -17.78, -25.4, 0),
    ("15", "PA4", "bidirectional",  17.78, 12.7, 180),
    ("16", "PA5", "bidirectional",  17.78, 10.16, 180),
    ("17", "PA7", "bidirectional",  17.78, 5.08, 180),
    ("18", "PB3/CMP3 out", "bidirectional",  17.78, 2.54, 180),
    ("19", "PB4", "bidirectional",  17.78, 0.0, 180),
    ("20", "PB1/PB5", "bidirectional",  17.78, -2.54, 180),
    ("21", "PB6", "bidirectional",  17.78, -5.08, 180),
    ("22", "PB7", "bidirectional",  17.78, -7.62, 180),
    ("23", "PB8", "bidirectional",  17.78, -10.16, 180),
    ("24", "PB9", "bidirectional",  17.78, -12.7, 180),
    ("25", "PB10", "bidirectional",  17.78, -15.24, 180),
    ("26", "PB11", "bidirectional",  17.78, -17.78, 180),
    ("27", "PB12", "bidirectional",  17.78, -20.32, 180),
    ("6", "VDD", "power_in", 0.0, 17.78, 270),
    ("7", "GND", "power_in", 0.0, -30.48, 90),
]


def mcu_symbol(name):
    s = (f'  (symbol "{name}" (pin_names (offset 1.016)) (exclude_from_sim no) (in_bom yes) (on_board yes)\n'
         f'    (property "Reference" "U" (at -10.16 16.51 0) (effects (font (size 1.27 1.27)) (justify left)))\n'
         f'    (property "Value" "{name}" (at 2.54 16.51 0) (effects (font (size 1.27 1.27)) (justify left)))\n'
         f'    (property "Footprint" "Package_SO:QSOP-28_3.9x9.9mm_P0.635mm" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))\n'
         f'    (property "Datasheet" "https://www.wch.cn/products/CH32X035.html" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))\n'
         f'    (property "Description" "WCH CH32X035G8R6, RISC-V, 62 KB flash, USB FS, 3 comparators, QSOP-28; pins grouped by use" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))\n'
         f'    (symbol "{name}_0_1" (rectangle (start -15.24 15.24) (end 15.24 -27.94) '
         f'(stroke (width 0.254) (type default)) (fill (type background))))\n'
         f'    (symbol "{name}_1_1"\n')
    for num, pname, typ, x, y, a in MCU_PINS:
        s += (f'      (pin {typ} line (at {x} {y} {a}) (length 2.54) (name "{pname}" (effects (font (size 1.27 1.27)))) '
              f'(number "{num}" (effects (font (size 1.27 1.27)))))\n')
    s += '    )\n    (embedded_fonts no)\n  )\n'
    return s


def write_symlib():
    with open(SYMLIB, "w") as f:
        f.write('(kicad_symbol_lib (version 20241209) (generator "calibrator-gen") (generator_version "9.0")\n')
        f.write(mcu_symbol("CH32X035G8R6"))
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
    # USB-C: power + update
    "J1": (35.56, 66.04, 0), "C1": (63.5, 43.18, 0), "R3": (72.39, 35.56, 90),
    "C2": (83.82, 43.18, 0), "C3": (91.44, 43.18, 0), "R1": (60.96, 55.88, 90), "R2": (71.12, 58.42, 90),
    "R9": (78.74, 72.39, 0), "SW1": (78.74, 83.82, 270),
    # Light sensors
    "Q2": (114.3, 88.89999999999999, 0), "R7": (127.0, 106.67999999999999, 0), "C4": (137.16, 106.67999999999999, 0),
    "Q3": (114.3, 121.92, 0), "R8": (127.0, 139.70000000000002, 0), "C5": (137.16, 139.70000000000002, 0),
    # MCU
    "U1": (185.42, 78.74, 0),
    # Flash + click
    "R4": (223.52, 66.04, 90), "D1": (238.76, 66.04, 180),
    "R5": (223.52, 104.14, 90), "R6": (231.14, 111.76, 0), "Q1": (238.76, 104.14, 0),
    "D2": (241.29999999999998, 88.9, 270), "BZ1": (254.0, 93.98, 0),
}

# Reference / value text position per part: (dx, dy, justify) twice.
FIELDS = {
    "J1": ((0, -27.94, None), (0, -25.4, None)),
    "U1": ((-15.24, 39.37, "left"), (-15.24, 41.91, "left")),
    "D1": ((0, -6.35, None), (0, -3.81, None)),
    "D2": ((-2.54, -1.27, "left"), (-2.54, 1.27, "left")),
    "SW1": ((-2.54, -1.27, "left"), (-2.54, 1.27, "left")),
    "Q1": ((5.08, -1.27, "left"), (5.08, 1.27, "left")),
    "Q2": ((5.08, -1.27, "left"), (5.08, 1.27, "left")),
    "Q3": ((5.08, -1.27, "left"), (5.08, 1.27, "left")),
    "BZ1": ((2.54, -5.08, "left"), (2.54, 5.08, "left")),
    "R1": ((-2.54, -1.905, None), (2.54, -1.905, None)),
    "R2": ((-2.54, 2.54, None), (2.54, 2.54, None)),
    "R3": ((0, -2.54, None), (0, 2.54, None)),
    "R4": ((0, -2.54, None), (0, 2.54, None)),
    "R5": ((0, -2.54, None), (0, 2.54, None)),
}

# Wires (polylines). Every T is drawn as segments meeting at one point.
WIRES = [
    # --- USB-C: power + update
    [(50.8, 50.8), (55.88, 50.8)], [(55.88, 50.8), (55.88, 35.56)],           # VBUS from J1
    [(55.88, 35.56), (63.5, 35.56)], [(63.5, 35.56), (68.58, 35.56)],            # VBUS rail
    [(63.5, 35.56), (63.5, 39.370000000000005)],                                       # C1
    [(76.2, 35.56), (83.82, 35.56)], [(83.82, 35.56), (91.44, 35.56)],           # VDD rail
    [(91.44, 35.56), (99.06, 35.56)],
    [(83.82, 35.56), (83.82, 39.370000000000005)], [(91.44, 35.56), (91.44, 39.370000000000005)],
    [(63.5, 46.99), (63.5, 48.260000000000005)], [(83.82, 46.99), (83.82, 48.260000000000005)],
    [(91.44, 46.99), (91.44, 48.260000000000005)],
    [(50.8, 55.88), (57.15, 55.88)], [(50.8, 58.42), (67.31, 58.42)],        # CC1 -> R1, CC2 -> R2
    [(64.77, 55.88), (78.74, 55.88)], [(78.74, 55.88), (78.74, 58.42)], [(74.93, 58.42), (78.74, 58.42)],
    [(50.8, 63.5), (53.34, 63.5)], [(50.8, 66.04), (53.34, 66.04)],          # D- (A7, B7)
    [(53.34, 63.5), (53.34, 66.04)], [(53.34, 66.04), (167.64, 66.04)],
    [(50.8, 68.58), (55.88, 68.58)], [(50.8, 71.12), (55.88, 71.12)],        # D+ (A6, B6)
    [(55.88, 68.58), (55.88, 71.12)], [(55.88, 68.58), (78.74, 68.58)],
    [(78.74, 68.58), (167.64, 68.58)],
    [(78.74, 76.2), (78.74, 78.74)], [(78.74, 88.9), (78.74, 91.44)],        # boot: D+ - R9 - SW1 - VDD
    [(35.56, 88.9), (35.56, 93.98)], [(27.94, 88.9), (27.94, 93.98), (35.56, 93.98)],  # GND + shield
    # --- Light sensors
    [(116.84, 83.82), (116.84, 81.28)],
    [(116.84, 116.84), (116.84, 114.3)],
    [(116.84, 93.98), (116.84, 99.06)],
    [(116.84, 99.06), (127.0, 99.06)],
    [(127.0, 99.06), (137.16, 99.06)],
    [(137.16, 99.06), (152.4, 99.06)],
    [(127.0, 99.06), (127.0, 102.87)],
    [(137.16, 99.06), (137.16, 102.87)],
    [(127.0, 110.49), (127.0, 113.03)],
    [(137.16, 110.49), (137.16, 113.03)],
    [(116.84, 127.0), (116.84, 132.08)],
    [(116.84, 132.08), (127.0, 132.08)],
    [(127.0, 132.08), (137.16, 132.08)],
    [(137.16, 132.08), (157.48, 132.08)],
    [(127.0, 132.08), (127.0, 135.89)],
    [(137.16, 132.08), (137.16, 135.89)],
    [(127.0, 143.51), (127.0, 146.05)],
    [(137.16, 143.51), (137.16, 146.05)],
    [(152.4, 99.06), (152.4, 78.74)],
    [(152.4, 78.74), (152.4, 76.2)],
    [(152.4, 76.2), (167.64, 76.2)],
    [(152.4, 78.74), (167.64, 78.74)],
    [(157.48, 132.08), (157.48, 81.28), (167.64, 81.28)],
    # --- MCU
    [(185.42, 60.96), (185.42, 58.42)], [(185.42, 109.22), (185.42, 111.76)],
    # --- Flash + click
    [(203.2, 66.04), (219.71, 66.04)],
    [(227.33, 66.04), (234.95, 66.04)],
    [(242.57, 66.04), (247.65, 66.04), (247.65, 68.58)],
    [(203.2, 68.58), (208.28, 68.58), (208.28, 104.14), (219.71, 104.14)],
    [(227.33, 104.14), (231.14, 104.14)],
    [(231.14, 104.14), (233.68, 104.14)],
    [(231.14, 104.14), (231.14, 107.95)],
    [(231.14, 115.57), (231.14, 118.11)],
    [(241.3, 109.22), (241.3, 111.76)],
    [(241.3, 99.06), (241.3, 96.52)],
    [(241.3, 96.52), (241.3, 92.71)],
    [(241.3, 96.52), (251.46, 96.52)],
    [(241.3, 85.09), (241.3, 81.28)],
    [(241.3, 81.28), (246.38, 81.28)],
    [(246.38, 81.28), (246.38, 91.44), (251.46, 91.44)],
]

JUNCTIONS = [(55.88, 35.56), (63.5, 35.56), (83.82, 35.56), (91.44, 35.56), (53.34, 66.04), (55.88, 68.58),
             (78.74, 68.58), (78.74, 58.42), (127.0, 99.06), (137.16, 99.06), (152.4, 78.74),
             (127.0, 132.08), (137.16, 132.08), (231.14, 104.14), (241.29999999999998, 96.52)]

POWER = [  # (lib_id, x, y, rotation)
    ("power:VBUS", 55.88, 35.56, 0), ("power:VDD", 99.06, 35.56, 0),
    ("power:GND", 35.56, 93.98, 0), ("power:GND", 63.5, 48.260000000000005, 0), ("power:GND", 83.82, 48.260000000000005, 0),
    ("power:GND", 91.44, 48.260000000000005, 0), ("power:GND", 78.74, 58.42, 0), ("power:VDD", 78.74, 91.44, 180),
    ("power:VDD", 116.84, 81.28, 0), ("power:VDD", 116.84, 114.3, 0),
    ("power:GND", 127.0, 113.03, 0), ("power:GND", 137.16, 113.03, 0),
    ("power:GND", 127.0, 146.05, 0), ("power:GND", 137.16, 146.05, 0),
    ("power:VDD", 185.42, 58.42, 0), ("power:GND", 185.42, 111.76, 0),
    ("power:GND", 247.65, 68.58, 0), ("power:GND", 231.14, 118.11, 0), ("power:GND", 241.29999999999998, 111.76, 0),
    ("power:VBUS", 241.29999999999998, 81.28, 0),
]
FLAGS = [(55.88, 50.8), (27.94, 93.98), (83.82, 35.56)]  # VBUS, GND, VDD

LABELS = []  # everything is wired

BLOCKS = [  # (title, x1, y1, x2, y2)
    ("USB-C: power + update", 15.24, 25.4, 101.6, 104.14),
    ("Light sensors (display mode)", 101.6, 71.12, 162.56, 154.94),
    ("MCU", 162.56, 25.4, 210.82, 154.94),
    ("Flash + click (camera and display mode)", 210.82, 25.4, 279.4, 154.94),
]

NOTES = [
    (17.78, 107.95, "5.1k on CC1/CC2 asks the phone for 5 V."),
    (17.78, 110.49, "D+/D- go only to the MCU. The firmware never turns USB on,"),
    (17.78, 113.03, "so a phone sees no device. R3 + C2/C3 filter spikes; C2/C3 sit at U1."),
    (17.78, 118.11, "Update: hold SW1 while plugging into a PC. The ROM bootloader starts"),
    (17.78, 120.65, "only at power-on with PC17 (D+) high; R9 limits the current into D+."),
    (104.14, 158.75, "Reverse-gullwing parts look down through holes. CMP3 (hysteresis)"),
    (104.14, 161.29, "flips where A and B see the same light; ADC0/ADC2 read dark / light."),
    (213.36, 158.75, "Reverse-mount white LED shines through a hole (~12 mA)."),
    (213.36, 161.29, "PA4 and PA5 are set by one register write."),
    (213.36, 163.83, "D2 catches the coil kick; R6 keeps Q1 off during reset."),
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
        '  (title_block (title "AVsync calibrator") (date "2026-11-08") (rev "0.3")\n'
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
