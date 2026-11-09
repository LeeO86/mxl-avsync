"""Builds calibrator.kicad_pcb from design.py (placement, nets, outline, zones, silkscreen).

Steps (all inside the KiCad image, see scripts/build.sh):
  python3 gen_pcb.py place      -> unrouted board + calibrator.dsn for Freerouting
  python3 gen_pcb.py finish     -> imports calibrator.ses, fills zones, saves the board
"""
import os
import sys

import faulthandler
import pcbnew

faulthandler.enable()

import design as D

HERE = os.path.dirname(os.path.abspath(__file__))
PRJ = os.path.abspath(os.path.join(HERE, ".."))
PCB = os.path.join(PRJ, "calibrator.kicad_pcb")
DSN = os.path.join(PRJ, "calibrator.dsn")
SES = os.path.join(PRJ, "calibrator.ses")
STOCK = "/usr/share/kicad/footprints"

OX, OY = D.ORIGIN


def mm(x, y):
    return pcbnew.VECTOR2I(pcbnew.FromMM(OX + x), pcbnew.FromMM(OY + y))


def seg(board, layer, x1, y1, x2, y2, w=0.12):
    s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
    s.SetLayer(layer)
    s.SetStart(mm(x1, y1))
    s.SetEnd(mm(x2, y2))
    s.SetWidth(pcbnew.FromMM(w))
    board.Add(s)


def arc(board, layer, a, m, b, w=0.12):
    s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_ARC)
    s.SetLayer(layer)
    s.SetArcGeometry(mm(*a), mm(*m), mm(*b))
    s.SetWidth(pcbnew.FromMM(w))
    board.Add(s)


def circle(board, layer, cx, cy, r, w=0.15):
    s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_CIRCLE)
    s.SetLayer(layer)
    s.SetCenter(mm(cx, cy))
    s.SetEnd(mm(cx + r, cy))
    s.SetWidth(pcbnew.FromMM(w))
    s.SetFilled(False)
    board.Add(s)


def text(board, layer, s, x, y, size=0.8, angle=0, bold=False):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s)
    t.SetLayer(layer)
    t.SetPosition(mm(x, y))
    t.SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(size), pcbnew.FromMM(size)))
    t.SetTextThickness(pcbnew.FromMM(max(0.155, size * 0.15 * (1.4 if bold else 1.0))))
    t.SetTextAngleDegrees(angle)
    if layer == pcbnew.B_SilkS:
        t.SetMirrored(True)
    board.Add(t)


def outline(board):
    W, H, r, n, sy = D.BOARD_W, D.BOARD_H, D.CORNER_R, D.NOTCH_R, D.SPLIT_Y
    e = pcbnew.Edge_Cuts
    c = 0.7071 * r
    seg(board, e, r, 0, W - r, 0, 0.05)
    arc(board, e, (W - r, 0), (W - r + c, r - c), (W, r), 0.05)
    seg(board, e, W, r, W, H - r, 0.05)
    arc(board, e, (W, H - r), (W - r + c, H - r + c), (W - r, H), 0.05)
    seg(board, e, W - r, H, r, H, 0.05)
    arc(board, e, (r, H), (r - c, H - r + c), (0, H - r), 0.05)
    seg(board, e, 0, H - r, 0, sy + n, 0.05)
    arc(board, e, (0, sy + n), (n, sy), (0, sy - n), 0.05)  # alignment notch on the split line
    seg(board, e, 0, sy - n, 0, r, 0.05)
    arc(board, e, (0, r), (r - c, r - c), (r, 0), 0.05)


def zone(board, net, layer):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNet(net)
    z.SetLocalClearance(pcbnew.FromMM(0.25))
    z.SetMinThickness(pcbnew.FromMM(0.2))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
    z.SetThermalReliefGap(pcbnew.FromMM(0.25))
    z.SetThermalReliefSpokeWidth(pcbnew.FromMM(0.3))
    z.SetAssignedPriority(0)
    z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    pts = [(0.2, 0.2), (D.BOARD_W - 0.2, 0.2), (D.BOARD_W - 0.2, D.BOARD_H - 0.2), (0.2, D.BOARD_H - 0.2)]
    poly = z.Outline()
    poly.NewOutline()
    for x, y in pts:
        poly.Append(pcbnew.FromMM(OX + x), pcbnew.FromMM(OY + y))
    board.Add(z)


def keepout(board, x1, y1, x2, y2, layer=None, tracks=True):
    """No tracks or vias here (optical holes, gold logo copper)."""
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True)
    if layer is None:
        z.SetLayerSet(pcbnew.LSET.AllCuMask())
    else:
        z.SetLayer(layer)
    z.SetDoNotAllowTracks(tracks)
    z.SetDoNotAllowVias(True)
    z.SetDoNotAllowPads(False)
    z.SetDoNotAllowFootprints(False)
    z.SetDoNotAllowCopperPour(False)
    poly = z.Outline()
    poly.NewOutline()
    for x, y in [(x1, y1), (x2, y1), (x2, y2), (x1, y2)]:
        poly.Append(pcbnew.FromMM(OX + x), pcbnew.FromMM(OY + y))
    board.Add(z)


# ---------------------------------------------------------------- artwork
ICON_S = 1.2427184466  # scale of the artwork group in assets/app-icon.svg
JLC_TEXT = 1.0         # JLC minimum silkscreen text height (mm)
JLC_LINE = 0.153       # JLC minimum silkscreen line width (mm)


def shape_poly(board, layer, pts, w):
    for a, b in zip(pts, pts[1:]):
        seg(board, layer, a[0], a[1], b[0], b[1], w)


def disc(board, layer, cx, cy, r):
    s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_CIRCLE)
    s.SetLayer(layer)
    s.SetCenter(mm(cx, cy))
    s.SetEnd(mm(cx + r, cy))
    s.SetWidth(0)
    s.SetFilled(True)
    board.Add(s)


def box(board, layer, x1, y1, x2, y2):
    s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_RECT)
    s.SetLayer(layer)
    s.SetStart(mm(x1, y1))
    s.SetEnd(mm(x2, y2))
    s.SetWidth(0)
    s.SetFilled(True)
    board.Add(s)


def logo(board, cx, cy, size, side):
    """AVsync mark from assets/app-icon.svg (CERN-OHL-P-2.0 on this board).

    Cyan/white strokes -> silkscreen. Orange parts (waveform, ring of the centre dot) ->
    bare copper with a solder-mask opening and no paste, so ENIG makes them gold.
    The copper has no net and is kept clear of tracks by a keep-out. On the bottom the
    mark is mirrored so it reads correctly from below."""
    silk, cu, mask = ((pcbnew.F_SilkS, pcbnew.F_Cu, pcbnew.F_Mask) if side == "F"
                      else (pcbnew.B_SilkS, pcbnew.B_Cu, pcbnew.B_Mask))
    k = size / 149.2 * ICON_S  # mm per SVG unit (the artwork spans 149.2 scaled units)
    sx = -1 if side == "B" else 1

    def P(u, v):
        return cx + sx * (u - 128) * k, cy + (v - 128) * k

    def W(w):
        return max(JLC_LINE, w * k)

    for poly in ([(68, 91), (68, 68), (91, 68)], [(165, 68), (188, 68), (188, 91)],
                 [(68, 165), (68, 188), (91, 188)], [(165, 188), (188, 188), (188, 165)]):
        shape_poly(board, silk, [P(*q) for q in poly], W(9))
    # meter panel: rounded rectangle 84,88 - 172,168, r 9
    x1, y1, x2, y2, r = 84, 88, 172, 168, 9
    c = 0.7071 * r
    shape_poly(board, silk, [P(x1 + r, y1), P(x2 - r, y1)], W(3))
    shape_poly(board, silk, [P(x2, y1 + r), P(x2, y2 - r)], W(3))
    shape_poly(board, silk, [P(x2 - r, y2), P(x1 + r, y2)], W(3))
    shape_poly(board, silk, [P(x1, y2 - r), P(x1, y1 + r)], W(3))
    for (ax, ay), (mx, my), (bx, by) in [((x2 - r, y1), (x2 - r + c, y1 + r - c), (x2, y1 + r)),
                                         ((x2, y2 - r), (x2 - r + c, y2 - r + c), (x2 - r, y2)),
                                         ((x1 + r, y2), (x1 + r - c, y2 - r + c), (x1, y2 - r)),
                                         ((x1, y1 + r), (x1 + r - c, y1 + r - c), (x1 + r, y1))]:
        arc(board, silk, P(ax, ay), P(mx, my), P(bx, by), W(3))
    # Dashed centre line like the original (y 74..181, dasharray 4 7). In the SVG the
    # centre dot is drawn on top, so dash parts under the dot/ring are hidden there too.
    dashes = []
    v = 74.0
    while v < 181:
        lo, hi = v, min(v + 4, 181)
        if hi <= 128 - 9 - 1.5 or lo >= 128 + 9 + 1.5:   # outside the ring (+ room for the round ends)
            dashes.append((lo, hi))
        v += 11
    dw = W(2.5)
    for lo, hi in dashes:
        # dash drawn without its round caps going past lo/hi (SVG dashes are butt-ended)
        cap = dw / 2 / k
        shape_poly(board, silk, [P(128, lo + cap), P(128, max(lo + cap, hi - cap))], dw)
    shape_poly(board, silk, [P(94, 199), P(162, 199)], W(4))

    # Gold copper: waveform + ring as one polygon. White silkscreen (dashes, centre dot)
    # sits on top in the original; JLC removes silk from bare copper, so the copper (and
    # its mask opening) is cut away under every dash and inside the ring.
    err = pcbnew.FromMM(0.005)
    gold = pcbnew.SHAPE_POLY_SET()
    pts = [(91, 128), (102, 128), (108, 113), (118, 145), (128, 102),
           (139, 148), (149, 117), (155, 128), (165, 128)]
    tmp = []
    for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
        s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(mm(*P(x1, y1)))
        s.SetEnd(mm(*P(x2, y2)))
        s.SetWidth(pcbnew.FromMM(5 * k))
        tmp.append(s)
    ring = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_CIRCLE)
    ring.SetCenter(mm(*P(128, 128)))
    ring.SetEnd(mm(*P(128 + 7, 128)))
    ring.SetWidth(pcbnew.FromMM(4 * k))
    ring.SetFilled(False)
    tmp.append(ring)
    for s in tmp:
        s.TransformShapeToPolygon(gold, cu, 0, err, pcbnew.ERROR_INSIDE)
    gold.Simplify()
    cut = pcbnew.SHAPE_POLY_SET()
    clear = 0.1  # copper stays 0.1 mm from white silk (mask opening then 0.05 mm)
    for lo, hi in dashes:
        xa, ya = P(128, lo)
        xb, yb = P(128, hi)
        hw = dw / 2 + clear
        o = cut.NewOutline()
        for cx_, cy_ in ((xa - hw, ya - clear), (xa + hw, ya - clear), (xb + hw, yb + clear), (xb - hw, yb + clear)):
            cut.Append(pcbnew.FromMM(OX + cx_), pcbnew.FromMM(OY + cy_), o)
    inner = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_CIRCLE)  # inside of the ring stays white
    inner.SetCenter(mm(*P(128, 128)))
    inner.SetEnd(mm(*P(128 + 5, 128)))
    inner.SetFilled(True)
    inner.SetWidth(0)
    inner.TransformShapeToPolygon(cut, cu, 0, err, pcbnew.ERROR_OUTSIDE)
    cut.Simplify()
    gold.BooleanSubtract(cut)
    gold.Fracture()
    mask_poly = pcbnew.SHAPE_POLY_SET(gold)
    mask_poly.Inflate(pcbnew.FromMM(0.05), pcbnew.CORNER_STRATEGY_ROUND_ALL_CORNERS, err)
    mask_poly.Fracture()
    for layer, poly in ((cu, gold), (mask, mask_poly)):
        s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_POLY)
        s.SetLayer(layer)
        s.SetPolyShape(poly)
        s.SetFilled(True)
        s.SetWidth(0)
        board.Add(s)
    # white centre dot: fills the inside of the gold ring, clear of the mask opening
    disc(board, silk, *P(128, 128), 5 * k - 0.05 - 0.05)


def clip_outside(x1, y1, x2, y2, r):
    """Parts of the segment (x1,y1)-(x2,y2) that lie outside the circle of radius r at 0,0."""
    dx, dy = x2 - x1, y2 - y1
    a = dx * dx + dy * dy
    b = 2 * (x1 * dx + y1 * dy)
    c = x1 * x1 + y1 * y1 - r * r
    disc_ = b * b - 4 * a * c
    if disc_ <= 0:
        return [(x1, y1, x2, y2)]
    s = disc_ ** 0.5
    t1, t2 = (-b - s) / (2 * a), (-b + s) / (2 * a)
    out = []
    if t1 > 0:
        e = min(t1, 1)
        out.append((x1, y1, x1 + dx * e, y1 + dy * e))
    if t2 < 1:
        st = max(t2, 0)
        out.append((x1 + dx * st, y1 + dy * st, x2, y2))
    return [o for o in out if (o[2] - o[0]) ** 2 + (o[3] - o[1]) ** 2 > 1e-6]


def qr(board, x0, y0, module):
    """QR code on the bottom silkscreen: white quiet zone + white light modules on the
    dark mask, mirrored so it reads correctly when you look at the bottom."""
    rows = [r for r in open(os.path.join(HERE, "qr_matrix.txt")).read().split() if r[0] in "01"]
    n = len(rows)
    q = 4  # quiet zone in modules
    tot = n + 2 * q
    for ry in range(tot):
        run = None
        for cxm in range(tot + 1):
            r_, c_ = ry - q, cxm - q
            light = cxm < tot and not (0 <= r_ < n and 0 <= c_ < n and rows[r_][c_] == "1")
            if light and run is None:
                run = cxm
            if not light and run is not None:
                # mirror in x: module column c is drawn at tot-1-c
                xa = x0 + (tot - cxm) * module
                xb = x0 + (tot - run) * module
                box(board, pcbnew.B_SilkS, xa, y0 + ry * module, xb, y0 + (ry + 1) * module)
                run = None
    return tot * module


def silk(board):
    F, B = pcbnew.F_SilkS, pcbnew.B_SilkS
    sy = D.SPLIT_Y
    lx, ly = D.PARTS["D1"][5][0], D.PARTS["D1"][5][1]
    q2 = D.PARTS["Q2"][5]
    q3 = D.PARTS["Q3"][5]
    for layer in (F, B):
        circle(board, layer, lx, ly, 2.7, 0.2)            # LED spot: put this on the camera lens
        text(board, layer, "A", q2[0], q2[1] - 2.7, 1.3, bold=True)
        text(board, layer, "B", q3[0], q3[1] + 2.7, 1.3, bold=True)
    # split line: on top it stops at the LED pads and continues behind the buzzer
    for x1, x2 in ((1.5, 2.05), (6.75, 9.3), (19.8, 22.4)):
        seg(board, F, x1, sy, x2, sy, 0.25)
    seg(board, F, 22.4, sy - 0.7, 22.4, sy + 0.7, 0.25)
    for x1, x2 in ((1.5, 3.1), (5.7, 10.6)):
        seg(board, B, x1, sy, x2, sy, 0.25)
    seg(board, B, 10.6, sy - 0.7, 10.6, sy + 0.7, 0.25)

    # Top: logo, name, licence, centred in the column between MCU and USB-C.
    cx = TOP_LOGO[0]
    logo(board, cx, TOP_LOGO[1], TOP_LOGO[2], "F")
    ty = TOP_LOGO[1] + TOP_LOGO[2] / 2
    text(board, F, "AVsync", cx, ty + 2.8, 2.0, bold=True)
    text(board, F, "calibrator", cx, ty + 5.2, 1.2)
    text(board, F, "CERN-OHL-P-2.0", 32.8, 20.7, JLC_TEXT)  # small, below the MCU
    # BOOT button label (top, centred under the button)
    sx, sy_ = D.PARTS["SW1"][5][:2]
    text(board, F, "BOOT", sx, sy_ + 4.5, 1.2, bold=True)

    # Bottom (this side touches the phone or the screen).
    # logo in the middle of the board, right beside the QR code
    bx, by, bs = BOT_LOGO
    logo(board, bx, by, bs, "B")
    text(board, B, "AVsync", bx, by + bs / 2 + 2.2, 1.8, bold=True)
    text(board, B, "calibrator v0.3", bx, by + bs / 2 + 4.3, JLC_TEXT)
    # how to use it, next to the LED / sensor end
    ix = 16.8
    text(board, B, "CAMERA", ix, 4.0, 1.2, bold=True)
    text(board, B, "LED on lens", ix, 5.8, JLC_TEXT)
    text(board, B, "SCREEN", ix, 14.0, 1.2, bold=True)
    text(board, B, "A|B on line", ix, 15.8, JLC_TEXT)
    text(board, B, "not a toy", ix, 18.8, JLC_TEXT)
    qr(board, QR[0], QR[1], QR[2])


# Artwork places (board mm): logo (centre x, centre y, size), QR (left x, top y, module)
TOP_LOGO = (50.3, 8.8, 9.0)
BOT_LOGO = (31.8, 9.0, 11.0)
QR = (40.0, 2.675, 0.45)
QR_SIZE = (29 + 8) * QR[2]


def stackup(path):
    """1.2 mm board, black mask, white silk, ENIG (KiCad's Python API has no stackup calls)."""
    s = open(path).read()
    st = ('\t\t(stackup\n'
          '\t\t\t(layer "F.SilkS" (type "Top Silk Screen") (color "White"))\n'
          '\t\t\t(layer "F.Paste" (type "Top Solder Paste"))\n'
          '\t\t\t(layer "F.Mask" (type "Top Solder Mask") (color "Black") (thickness 0.01))\n'
          '\t\t\t(layer "F.Cu" (type "copper") (thickness 0.035))\n'
          '\t\t\t(layer "dielectric 1" (type "core") (thickness 1.11) (material "FR4") (epsilon_r 4.5) (loss_tangent 0.02))\n'
          '\t\t\t(layer "B.Cu" (type "copper") (thickness 0.035))\n'
          '\t\t\t(layer "B.Mask" (type "Bottom Solder Mask") (color "Black") (thickness 0.01))\n'
          '\t\t\t(layer "B.Paste" (type "Bottom Solder Paste"))\n'
          '\t\t\t(layer "B.SilkS" (type "Bottom Silk Screen") (color "White"))\n'
          '\t\t\t(copper_finish "ENIG")\n\t\t\t(dielectric_constraints no)\n\t\t)\n')
    if "(stackup" not in s:
        s = s.replace("\t(setup\n", "\t(setup\n" + st, 1)
        open(path, "w").write(s)


def place():
    with open(PCB, "w") as f:
        f.write('(kicad_pcb (version 20241229) (generator "pcbnew") (generator_version "9.0"))\n')
    board = pcbnew.LoadBoard(PCB)
    ds = board.GetDesignSettings()
    ds.SetBoardThickness(pcbnew.FromMM(D.PCB_THICKNESS))

    nets = {}
    for n in D.NETS:
        ni = pcbnew.NETINFO_ITEM(board, n)
        board.Add(ni)
        nets[n] = ni

    for ref, (lib_id, value, fpid, lcsc, netmap, (x, y, rot, side)) in D.PARTS.items():
        lib, name = fpid.split(":")
        path = os.path.join(PRJ, "calibrator.pretty") if lib == "calibrator" else os.path.join(STOCK, lib + ".pretty")
        fp = pcbnew.FootprintLoad(path, name)
        fp.SetFPID(pcbnew.LIB_ID(lib, name))
        fp.SetReference(ref)
        fp.SetValue(value)
        board.Add(fp)
        fp.SetPosition(mm(x, y))
        fp.SetOrientationDegrees(rot)
        if side == "B":
            fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
        if ref.startswith("TP"):
            fp.SetAttributes(fp.GetAttributes() | pcbnew.FP_EXCLUDE_FROM_POS_FILES | pcbnew.FP_EXCLUDE_FROM_BOM)
            fp.Reference().SetVisible(False)
        if lcsc:
            f = pcbnew.PCB_FIELD(fp, fp.GetNextFieldId(), "LCSC")
            f.SetText(lcsc)
            f.SetVisible(False)
            f.SetLayer(pcbnew.F_Fab)
            fp.Add(f)
        for pad in fp.Pads():
            num = pad.GetNumber()
            if num in netmap and netmap[num]:
                pad.SetNet(nets[netmap[num]])
        # small reference text, kept on the fab layer so the top stays clean for the logo
        fp.Reference().SetLayer(pcbnew.F_Fab if side == "F" else pcbnew.B_Fab)
        # The KiCad image ships no 3D model for these three; use stand-ins for the renders.
        stand_in = {"J1": "${KICAD9_3DMODEL_DIR}/Connector_USB.3dshapes/"
                          "USB_C_Receptacle_GCT_USB4105-xx-A_16P_TopMnt_Horizontal.step",
                    "U1": "${KIPRJMOD}/3d/QSOP-28.wrl", "SW1": "${KIPRJMOD}/3d/TS-1187A.wrl"}
        if ref in stand_in:
            fp.Models().clear()
            m = pcbnew.FP_3DMODEL()
            m.m_Filename = stand_in[ref]
            fp.Models().push_back(m)
        if ref == "J1":  # the stock USB-C footprint's silkscreen runs past the board edge
            for item in list(fp.GraphicalItems()):
                if item.GetLayer() == pcbnew.F_SilkS:
                    fp.Remove(item)

    outline(board)
    # keep tracks and vias off the optical holes (LED cut-out and sensor holes)
    lx, ly = D.PARTS["D1"][5][:2]
    keepout(board, lx - 1.35, ly - 1.15, lx + 1.35, ly + 1.15)
    keepout(board, 0.0, D.SPLIT_Y - 1.9, 2.3, D.SPLIT_Y + 1.9)  # alignment notch
    keepout(board, QR[0], QR[1], QR[0] + QR_SIZE, QR[1] + QR_SIZE, tracks=False)  # no via holes in the QR code
    for q in ("Q2", "Q3"):
        qx, qy = D.PARTS[q][5][:2]
        keepout(board, qx - 1.45, qy - 1.45, qx + 1.45, qy + 1.45)
    tx, ty, ts = TOP_LOGO
    keepout(board, tx - ts / 2 - 0.5, ty - ts / 2 - 0.5, tx + ts / 2 + 0.5, ty + ts / 2 + 0.5, pcbnew.F_Cu)
    keepout(board, tx - ts / 2 - 1.0, ty - ts / 2 - 1.0, tx + ts / 2 + 1.0, ty + ts / 2 + 6.0, tracks=False)  # no via holes in the top logo or its text
    bx, by, bs = BOT_LOGO
    keepout(board, bx - bs / 2 - 0.5, by - bs / 2 - 0.5, bx + bs / 2 + 0.5, by + bs / 2 + 0.5, pcbnew.B_Cu)
    keepout(board, bx - bs / 2 - 1.0, by - bs / 2 - 1.0, bx + bs / 2 + 1.0, by + bs / 2 + 5.5, tracks=False)  # no via holes in the bottom logo or its text
    silk(board)
    pcbnew.SaveBoard(PCB, board)
    stackup(PCB)
    board = pcbnew.LoadBoard(PCB)
    # Export for the router *before* the ground fills exist, so it also routes GND
    # with real tracks (with fills it treats GND as a plane and leaves islands).
    ok = pcbnew.ExportSpecctraDSN(board, DSN)
    gnd = board.FindNet("GND")
    zone(board, gnd, pcbnew.F_Cu)
    zone(board, gnd, pcbnew.B_Cu)
    pcbnew.SaveBoard(PCB, board)
    print("placed, DSN export:", ok)


def add_via(board, net, x, y):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(mm(x, y))
    v.SetWidth(pcbnew.FromMM(0.6))
    v.SetDrill(pcbnew.FromMM(0.3))
    v.SetNet(net)
    board.Add(v)


# Areas kept free of stitching vias so the silkscreen text stays whole (x1, y1, x2, y2).
NO_STITCH = [(43.5, 0.0, 57.0, 22.0), (36.5, 2.5, 44.5, 13.5), (25.0, 2.0, 39.6, 20.5), (11.0, 2.5, 25.0, 21.5),
             (39.7, 2.4, 56.9, 19.6), (0.0, 9.8, 23.0, 12.2), (24.0, 19.6, 39.0, 21.9)]


def stitch(board, net, step=2.5, margin=0.7):
    """Put GND vias on a grid wherever both ground fills have room for one.

    This ties every piece of the top fill to the bottom fill."""
    fills = [z.GetFilledPolysList(z.GetLayer()) for z in board.Zones()
             if not z.GetIsRuleArea() and z.GetNetname() == "GND"]
    n = 0
    y = 1.4
    while y < D.BOARD_H - 1.2:
        x = 10.5  # no vias on the optics end: open via holes could let light through
        while x < D.BOARD_W - 1.2:
            if any(a <= x <= c and b <= y <= d for a, b, c, d in NO_STITCH):
                x += step
                continue
            pts = [(x, y)] + [(x + margin * dx, y + margin * dy) for dx, dy in
                              [(1, 0), (-1, 0), (0, 1), (0, -1), (0.7, 0.7), (-0.7, 0.7), (0.7, -0.7), (-0.7, -0.7)]]
            if all(f.Contains(mm(px, py)) for f in fills for (px, py) in pts):
                add_via(board, net, x, y)
                n += 1
            x += step
        y += step
    return n


# Silkscreen art on the bottom: no vias at all here.
ART = [(39.7, 2.4, 56.9, 19.6), (25.0, 2.0, 39.6, 20.5), (43.5, 0.0, 57.0, 22.0), (11.0, 2.5, 25.0, 21.5)]


def fix_islands(board, net, margin=0.31):
    """Give every top-fill island that has no via one via, at the first spot that fits."""
    zs = {z.GetLayer(): z for z in board.Zones() if not z.GetIsRuleArea() and z.GetNetname() == "GND"}
    top = zs[pcbnew.F_Cu].GetFilledPolysList(pcbnew.F_Cu)
    bot = zs[pcbnew.B_Cu].GetFilledPolysList(pcbnew.B_Cu)
    vias = [t.GetPosition() for t in board.GetTracks() if isinstance(t, pcbnew.PCB_VIA)]
    added = 0
    for i in range(top.OutlineCount()):
        isl = top.Outline(i)
        if any(isl.PointInside(v) for v in vias):
            continue
        bb = isl.BBox()
        x0, y0 = pcbnew.ToMM(bb.GetX()) - OX, pcbnew.ToMM(bb.GetY()) - OY
        found = None
        steps = int(pcbnew.ToMM(bb.GetWidth()) / 0.1) + 1, int(pcbnew.ToMM(bb.GetHeight()) / 0.1) + 1
        for iy in range(steps[1]):
            for ix in range(steps[0]):
                x, y = x0 + ix * 0.1, y0 + iy * 0.1
                ring = [(x, y)] + [(x + margin * dx, y + margin * dy) for dx, dy in
                                   [(1, 0), (-1, 0), (0, 1), (0, -1), (0.7, 0.7), (-0.7, 0.7), (0.7, -0.7), (-0.7, -0.7)]]
                if any(a <= x <= c and b <= y <= d for a, b, c, d in ART):
                    continue
                if all(isl.PointInside(mm(px, py)) and bot.Contains(mm(px, py)) for px, py in ring):
                    found = (x, y)
                    break
            if found:
                break
        if found:
            add_via(board, net, *found)
            added += 1
        else:
            print("no room for a via in island at", round(x0, 2), round(y0, 2))
    return added


def remove_stubs(board):
    """Delete duplicate segments and short dead-end pieces the router sometimes leaves behind."""
    items = list(board.GetTracks())  # one snapshot; the board's list proxy breaks after Remove()
    tracks = [t for t in items if t.GetClass() == "PCB_TRACK"]
    vias = {(v.GetPosition().x, v.GetPosition().y) for v in items if v.GetClass() == "PCB_VIA"}
    pads = list(board.GetPads())
    dead = []
    seen = set()  # the same segment written twice (once each way)
    for t in tracks:
        a_, b_ = (t.GetStart().x, t.GetStart().y), (t.GetEnd().x, t.GetEnd().y)
        key = (int(t.GetLayer()), int(t.GetNetCode())) + tuple(sorted([a_, b_]))
        if key in seen:
            dead.append(t)
        seen.add(key)
    tracks = [t for t in tracks if t not in dead]
    while True:
        ends = {}
        for t in tracks:
            for p in (t.GetStart(), t.GetEnd()):
                ends.setdefault((p.x, p.y, int(t.GetLayer())), []).append(t)
        new = []
        for t in tracks:
            for p in (t.GetStart(), t.GetEnd()):
                if len(ends[(p.x, p.y, int(t.GetLayer()))]) > 1 or (p.x, p.y) in vias:
                    continue
                if any(pad.HitTest(p) and pad.IsOnLayer(t.GetLayer()) for pad in pads):
                    continue
                new.append(t)
                break
        if not new:
            break
        dead += new
        tracks = [t for t in tracks if t not in new]
    for t in dead:
        board.Remove(t)
    return len(dead)


def unconnected(board):
    board.BuildConnectivity()
    return board.GetConnectivity().GetUnconnectedCount(False)


def import_routes():
    """Separate step (own process): the Python board object is unsafe after Remove()."""
    board = pcbnew.LoadBoard(PCB)
    ok = pcbnew.ImportSpecctraSES(board, SES)
    print("SES import:", ok)
    print("removed stubs:", remove_stubs(board))
    pcbnew.SaveBoard(PCB, board)


def finish():
    board = pcbnew.LoadBoard(PCB)
    gnd = board.FindNet("GND")
    board.BuildConnectivity()
    filler = pcbnew.ZONE_FILLER(board)
    filler.Fill(board.Zones())
    print("stitching vias:", stitch(board, gnd))
    board.BuildConnectivity()
    filler.Fill(board.Zones())
    print("island vias:", fix_islands(board, gnd))
    board.BuildConnectivity()
    filler.Fill(board.Zones())
    print("unconnected after fill:", unconnected(board))
    pcbnew.SaveBoard(PCB, board)
    print("finished")


if __name__ == "__main__":
    {"place": place, "import": import_routes, "finish": finish}[sys.argv[1]]()
