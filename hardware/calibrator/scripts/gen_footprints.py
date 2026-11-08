"""Writes the four custom footprints and simple box 3D models.

Run: python3 gen_footprints.py   (plain Python, no KiCad needed)
Sources of the dimensions are in each footprint's description.
"""
import math
import os
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.join(HERE, "..", "calibrator.pretty")
MODELS = os.path.join(HERE, "..", "3d")
# KiCad resolves ${KIPRJMOD} to the project folder.
MODEL_DIR = "${KIPRJMOD}/3d"


def u():
    return str(uuid.uuid5(uuid.NAMESPACE_URL, f"calibrator-{next(_n)}"))


def _counter():
    i = 0
    while True:
        i += 1
        yield i


_n = _counter()


def stroke(w):
    return f'(stroke (width {w}) (type solid))'


def line(x1, y1, x2, y2, layer, w=0.12):
    return f'  (fp_line (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) {stroke(w)} (layer "{layer}") (uuid "{u()}"))\n'


def rect(x1, y1, x2, y2, layer, w=0.05):
    return (line(x1, y1, x2, y1, layer, w) + line(x2, y1, x2, y2, layer, w)
            + line(x2, y2, x1, y2, layer, w) + line(x1, y2, x1, y1, layer, w))


def arc(cx, cy, r, a0, a1, layer, w=0.1):
    """Arc from angle a0 to a1 (degrees, screen coordinates, y down)."""
    def pt(a):
        return cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a))
    s, m, e = pt(a0), pt((a0 + a1) / 2), pt(a1)
    return (f'  (fp_arc (start {s[0]:.4f} {s[1]:.4f}) (mid {m[0]:.4f} {m[1]:.4f}) (end {e[0]:.4f} {e[1]:.4f}) '
            f'{stroke(w)} (layer "{layer}") (uuid "{u()}"))\n')


def circle(cx, cy, r, layer, w=0.12):
    return f'  (fp_circle (center {cx} {cy}) (end {cx + r} {cy}) {stroke(w)} (fill no) (layer "{layer}") (uuid "{u()}"))\n'


def rounded_cutout(w, h, r):
    """Closed rounded rectangle on Edge.Cuts, centred on 0,0."""
    x, y = w / 2, h / 2
    s = ""
    s += line(-x + r, -y, x - r, -y, "Edge.Cuts", 0.05)
    s += line(x, -y + r, x, y - r, "Edge.Cuts", 0.05)
    s += line(x - r, y, -x + r, y, "Edge.Cuts", 0.05)
    s += line(-x, y - r, -x, -y + r, "Edge.Cuts", 0.05)
    s += arc(x - r, -y + r, r, 270, 360, "Edge.Cuts", 0.05)
    s += arc(x - r, y - r, r, 0, 90, "Edge.Cuts", 0.05)
    s += arc(-x + r, y - r, r, 90, 180, "Edge.Cuts", 0.05)
    s += arc(-x + r, -y + r, r, 180, 270, "Edge.Cuts", 0.05)
    return s


def smd(name, x, y, w, h, rot=0):
    return (f'  (pad "{name}" smd roundrect (at {x:.3f} {y:.3f} {rot}) (size {w} {h}) '
            f'(layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.15) (uuid "{u()}"))\n')


def text(kind, value, y, layer, hide=False):
    h = " (hide yes)" if hide else ""
    return (f'  (property "{kind}" "{value}" (at 0 {y} 0) (layer "{layer}"){h} (uuid "{u()}") '
            f'(effects (font (size 0.8 0.8) (thickness 0.12))))\n')


def model(name, z=0.0, rot=0):
    return (f'  (model "{MODEL_DIR}/{name}.wrl" (offset (xyz 0 0 {z})) (scale (xyz 1 1 1)) '
            f'(rotate (xyz 0 0 {rot})))\n')


def footprint(name, descr, body, ref_y, val_y, attr="smd"):
    s = (f'(footprint "{name}"\n  (version 20241229)\n  (generator "calibrator-gen")\n  (generator_version "9.0")\n'
         f'  (layer "F.Cu")\n  (descr "{descr}")\n')
    s += text("Reference", "REF**", ref_y, "F.SilkS")
    s += text("Value", name, val_y, "F.Fab")
    s += text("Datasheet", "", 0, "F.Fab", hide=True)
    s += text("Description", "", 0, "F.Fab", hide=True)
    s += f'  (attr {attr})\n'
    s += body
    s += ")\n"
    with open(os.path.join(LIB, name + ".kicad_mod"), "w") as f:
        f.write(s)


def buzzer():
    # Huaneng MLT-8530 datasheet (LCSC C94599): 8.5 x 8.5 x 3.0 mm, 4 pads 2.3 x 2.3,
    # 4.8 mm gap -> pad centres at +-3.55. Pad 1 (+) top left, pad 2 (-) bottom left,
    # two dummy pads on the right. The sound port is on the right side (+x).
    b = ""
    b += smd("1", -3.55, -3.55, 2.3, 2.3)
    b += smd("2", -3.55, 3.55, 2.3, 2.3)
    b += smd("", 3.55, -3.55, 2.3, 2.3)
    b += smd("", 3.55, 3.55, 2.3, 2.3)
    b += rect(-4.25, -4.25, 4.25, 4.25, "F.Fab", 0.1)
    b += rect(-5.0, -5.0, 5.0, 5.0, "F.CrtYd", 0.05)
    # silkscreen: body sides between the pads, "+" mark and an arrow for the sound port
    b += line(-4.35, -2.2, -4.35, 2.2, "F.SilkS")
    b += line(4.35, -2.2, 4.35, 2.2, "F.SilkS")
    b += line(-2.2, -4.35, 2.2, -4.35, "F.SilkS")
    b += line(-2.2, 4.35, 2.2, 4.35, "F.SilkS")
    b += line(-5.6, -3.55, -4.9, -3.55, "F.SilkS")
    b += line(-5.25, -3.9, -5.25, -3.2, "F.SilkS")
    b += line(2.6, 0, 3.6, 0, "F.SilkS")
    b += line(3.6, 0, 3.1, -0.4, "F.SilkS")
    b += line(3.6, 0, 3.1, 0.4, "F.SilkS")
    b += model("MLT-8530")
    footprint("Buzzer_MLT-8530",
              "Magnetic buzzer Huaneng MLT-8530 8.5x8.5x3mm SMD, sound port on +x side, datasheet LCSC C94599",
              b, -5.6, 5.6)


def led_reverse():
    # XINGLIGHT XL-3216UWC-FB (LCSC C3646935): 3.2 x 1.6 mm substrate, lens 1.86 x 1.4 x 0.52 mm
    # sits in a board cut-out and shines through the board. Pad 1 = cathode (-), pad 2 = anode (+).
    # Cut-out 2.1 x 1.7 mm (r 0.5, router bit); pads 0.3 mm from the cut-out edge (JLC copper-to-edge).
    b = ""
    b += smd("1", -1.7, 0, 0.7, 1.5)
    b += smd("2", 1.7, 0, 0.7, 1.5)
    b += rounded_cutout(2.1, 1.7, 0.5)
    b += rect(-1.6, -0.8, 1.6, 0.8, "F.Fab", 0.1)
    b += rect(-2.3, -1.1, 2.3, 1.1, "F.CrtYd", 0.05)
    b += line(-2.45, -1.0, -2.45, 1.0, "F.SilkS")  # cathode side mark
    b += model("LED_reverse_1206")
    footprint("LED_1206_ReverseMount_XL-3216UWC-FB",
              "Reverse mount 1206 LED shining through a board cut-out, XINGLIGHT XL-3216UWC-FB (LCSC C3646935); "
              "Lite-On LTW-C230DS fits the same footprint",
              b, -1.8, 1.8)


def phototransistor_rgw():
    # Vishay VEMT2500X01 (doc 81134), reverse gullwing: body above the board, dome lens (D 1.8 mm)
    # looks down through a D 2.3 mm hole. Solder pad proposal 1.7 x 0.75 at 6.7 mm overall.
    # Pad 1 = collector, pad 2 = emitter (tape drawing: lead I collector, lead II emitter).
    b = ""
    b += smd("1", -2.5, 0, 1.7, 0.75)
    b += smd("2", 2.5, 0, 1.7, 0.75)
    b += (f'  (pad "" np_thru_hole circle (at 0 0) (size 2.3 2.3) (drill 2.3) '
          f'(layers "*.Cu" "*.Mask") (uuid "{u()}"))\n')
    b += rect(-1.15, -1.15, 1.15, 1.15, "F.Fab", 0.1)
    b += rect(-3.6, -1.6, 3.6, 1.6, "F.CrtYd", 0.05)
    b += line(-1.25, -1.25, 1.25, -1.25, "F.SilkS")
    b += line(-1.25, 1.25, 1.25, 1.25, "F.SilkS")
    b += line(-1.25, -1.25, -1.25, -0.6, "F.SilkS")  # collector side mark
    b += model("VEMT2500X01_RGW")
    footprint("Vishay_VEMT2500X01_ReverseGullwing",
              "Phototransistor Vishay VEMT2500X01 reverse gullwing, lens looks through a 2.3 mm hole; "
              "VEMT2000X01 is the same package",
              b, -2.0, 2.0)


def usb_c_6p():
    # SHOU HAN TYPE-C 6P (LCSC C456012) recommended layout: signal pads at +-0.5 (CC), +-1.5 (VBUS),
    # +-2.7 (GND); shell slots 0.6 x 1.4 (pad 1.0 x 1.8) at +-4.32, 3.8 mm apart, front slots 2.6 mm
    # behind the product edge. Origin = body centre, like KiCad's GCT USB4125 6P footprint (same family).
    b = ""
    py, pl = -3.35, 1.4
    for name, x, w in [("A5", -0.5, 0.6), ("B5", 0.5, 0.6), ("B9", -1.5, 0.7), ("A9", 1.5, 0.7),
                       ("B12", -2.7, 0.8), ("A12", 2.7, 0.8)]:
        b += smd(name, x, py, w, pl)
    for x in (-4.32, 4.32):
        for y in (-3.0, 0.8):
            b += (f'  (pad "S1" thru_hole oval (at {x} {y}) (size 1.0 1.8) (drill oval 0.6 1.4) '
                  f'(layers "*.Cu" "*.Mask") (remove_unused_layers no) (uuid "{u()}"))\n')
    b += rect(-4.47, -3.4, 4.47, 3.4, "F.Fab", 0.1)
    b += rect(-5.2, -4.4, 5.2, 3.9, "F.CrtYd", 0.05)
    b += line(-4.6, -1.9, -4.6, -0.3, "F.SilkS")
    b += line(4.6, -1.9, 4.6, -0.3, "F.SilkS")
    b += line(-4.6, 1.9, -4.6, 2.7, "F.SilkS")
    b += line(4.6, 1.9, 4.6, 2.7, "F.SilkS")
    b += line(-5.0, 3.1, 5.0, 3.1, "Dwgs.User", 0.1)  # board edge
    b += ("  (model \"${KICAD9_3DMODEL_DIR}/Connector_USB.3dshapes/"
          "USB_C_Receptacle_GCT_USB4125-xx-x_6P_TopMnt_Horizontal.step\" (offset (xyz 0 0 0)) "
          "(scale (xyz 1 1 1)) (rotate (xyz 0 0 0)))\n")
    footprint("USB_C_Receptacle_ShouHan_TYPE-C-6P",
              "USB-C receptacle 6 pin power only, SHOU HAN TYPE-C 6P (LCSC C456012), top mount horizontal; "
              "board edge 3.1 mm in front of the body centre",
              b, -5.0, 4.6)


# ---- simple VRML box models (KiCad VRML unit = 0.1 inch) ----
def vrml(name, boxes):
    k = 1 / 2.54
    s = "#VRML V2.0 utf8\n"
    for (cx, cy, cz, sx, sy, sz, rgb) in boxes:
        s += (f"Transform {{ translation {cx * k:.4f} {cy * k:.4f} {cz * k:.4f} children [ Shape {{ "
              f"appearance Appearance {{ material Material {{ diffuseColor {rgb} }} }} "
              f"geometry Box {{ size {sx * k:.4f} {sy * k:.4f} {sz * k:.4f} }} }} ] }}\n")
    with open(os.path.join(MODELS, name + ".wrl"), "w") as f:
        f.write(s)


def models():
    # VRML y is up on screen = -y in KiCad; boxes are symmetric so it does not matter here.
    vrml("MLT-8530", [(0, 0, 1.5, 8.5, 8.5, 3.0, "0.08 0.08 0.08"), (-4.35, 0, 1.5, 0.3, 1.2, 1.0, "0.3 0.3 0.3")])
    vrml("LED_reverse_1206", [(0, 0, 0.14, 3.2, 1.6, 0.28, "0.9 0.9 0.85"), (0, 0, -0.26, 1.86, 1.4, 0.52, "1 1 0.6")])
    leads = [(x, sgn * 2.75, 0.4, 0.45, 1.3, 0.2, "0.8 0.8 0.8") for sgn in (-1, 1)
             for x in [-4.1275 + 0.635 * i for i in range(14)]]
    vrml("QSOP-28", [(0, 0, 0.85, 9.9, 3.9, 1.5, "0.12 0.12 0.12")] +
         [(x, y, z, sx, sy, sz, c) for (x, y, sx, sy, sz, z, c) in
          [(x, y, 0.45, 1.3, 0.2, 0.1, c) for (x, y, _a, _b, _c, _d, c) in leads]])
    vrml("TS-1187A", [(0, 0, 0.75, 5.1, 5.1, 1.5, "0.85 0.85 0.85"), (0, 0, 1.6, 3.0, 3.0, 0.3, "0.15 0.15 0.15")] +
         [(sx * 3.0, sy * 1.875, 0.1, 1.0, 0.75, 0.2, "0.8 0.8 0.8") for sx in (-1, 1) for sy in (-1, 1)])
    vrml("VEMT2500X01_RGW", [(0, 0, 0.8, 2.3, 2.3, 1.6, "0.75 0.8 0.85"), (0, 0, -0.58, 1.8, 1.8, 1.17, "0.7 0.75 0.8"),
                              (-2.5, 0, 0.1, 1.2, 0.4, 0.2, "0.8 0.8 0.8"), (2.5, 0, 0.1, 1.2, 0.4, 0.2, "0.8 0.8 0.8")])


if __name__ == "__main__":
    os.makedirs(LIB, exist_ok=True)
    os.makedirs(MODELS, exist_ok=True)
    buzzer()
    led_reverse()
    phototransistor_rgw()

    models()
    print("footprints and models written")
