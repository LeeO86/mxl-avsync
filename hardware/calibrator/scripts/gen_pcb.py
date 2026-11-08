"""Builds calibrator.kicad_pcb from design.py (placement, nets, outline, zones, silkscreen).

Steps (all inside the KiCad image, see scripts/build.sh):
  python3 gen_pcb.py place      -> unrouted board + calibrator.dsn for Freerouting
  python3 gen_pcb.py finish     -> imports calibrator.ses, fills zones, saves the board
"""
import os
import sys

import pcbnew

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


def keepout(board, x1, y1, x2, y2, layer=None):
    """No tracks or vias here (optical holes, gold logo copper)."""
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True)
    if layer is None:
        z.SetLayerSet(pcbnew.LSET.AllCuMask())
    else:
        z.SetLayer(layer)
    z.SetDoNotAllowTracks(True)
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
    # dashed centre line (dashes near the gold parts are left out)
    v = 74
    while v < 181:
        if not 95 <= v <= 152:
            shape_poly(board, silk, [P(128, v), P(128, min(v + 4, 181))], W(2.5))
        v += 11
    shape_poly(board, silk, [P(94, 199), P(162, 199)], W(4))
    # (the white centre dot is left out: at this size it would sit on the gold waveform)
    # gold: waveform and the ring of the centre dot
    wave = [P(*q) for q in [(91, 128), (102, 128), (108, 113), (118, 145), (128, 102),
                            (139, 148), (149, 117), (155, 128), (165, 128)]]
    for layer, extra in ((cu, 0.0), (mask, 0.1)):
        shape_poly(board, layer, wave, 5 * k + extra)
        circle(board, layer, *P(128, 128), 7 * k, 4 * k + extra)


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
    text(board, F, "AVsync", cx, 13.3, 2.0, bold=True)
    text(board, F, "calibrator", cx, 15.7, 1.2)
    text(board, F, "CERN-OHL-P-2.0", cx, 18.6, JLC_TEXT)

    # Bottom (this side touches the phone or the screen).
    bx = BOT_LOGO[0]
    logo(board, bx, BOT_LOGO[1], BOT_LOGO[2], "B")
    text(board, B, "AVsync", bx, 12.9, 1.8, bold=True)
    text(board, B, "calibrator v0.2", bx, 15.0, JLC_TEXT)
    text(board, B, "not a toy", bx, 17.0, JLC_TEXT)
    ix = 28.6
    text(board, B, "CAMERA", ix, 3.0, JLC_TEXT, bold=True)
    text(board, B, "LED on lens", ix, 4.6, JLC_TEXT)
    text(board, B, "SCREEN", ix, 7.2, JLC_TEXT, bold=True)
    text(board, B, "A|B on line", ix, 8.8, JLC_TEXT)
    for i, lab in enumerate(["5V", "GND", "DIO", "CLK", "RST"]):
        text(board, B, lab, 24.0 + 2.54 * i, 15.9, JLC_TEXT, angle=90)
    qr(board, QR[0], QR[1], QR[2])


# Artwork places (board mm): logo (centre x, centre y, size), QR (left x, top y, module)
TOP_LOGO = (43.25, 6.0, 9.0)
BOT_LOGO = (16.5, 6.4, 8.0)
QR = (35.9, 2.675, 0.45)
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

    outline(board)
    # keep tracks and vias off the optical holes (LED cut-out and sensor holes)
    lx, ly = D.PARTS["D1"][5][:2]
    keepout(board, lx - 1.35, ly - 1.15, lx + 1.35, ly + 1.15)
    for q in ("Q2", "Q3"):
        qx, qy = D.PARTS[q][5][:2]
        keepout(board, qx - 1.45, qy - 1.45, qx + 1.45, qy + 1.45)
    tx, ty, ts = TOP_LOGO
    keepout(board, tx - ts / 2 - 0.5, ty - ts / 2 - 0.5, tx + ts / 2 + 0.5, ty + ts / 2 + 0.5, pcbnew.F_Cu)
    bx, by, bs = BOT_LOGO
    keepout(board, bx - bs / 2 - 0.5, by - bs / 2 - 0.5, bx + bs / 2 + 0.5, by + bs / 2 + 0.5, pcbnew.B_Cu)
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
NO_STITCH = [(37.0, 0.0, 49.5, 22.0), (11.5, 1.6, 21.5, 18.0), (23.0, 1.6, 34.5, 10.0), (22.5, 14.0, 35.5, 20.0),
             (35.6, 2.4, 52.9, 19.6), (0.0, 9.8, 23.0, 12.2)]


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
ART = [(35.6, 2.4, 52.9, 19.6), (11.5, 1.6, 21.5, 18.0), (37.0, 0.0, 49.5, 22.0)]


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
    """Delete short dead-end track pieces the router sometimes leaves behind."""
    removed = 0
    while True:
        tracks = [t for t in board.GetTracks() if t.GetClass() == "PCB_TRACK"]
        ends = {}
        for t in tracks:
            for p in (t.GetStart(), t.GetEnd()):
                ends.setdefault((p.x, p.y, t.GetLayer()), []).append(t)
        vias = {(v.GetPosition().x, v.GetPosition().y) for v in board.GetTracks() if v.GetClass() == "PCB_VIA"}
        dead = []
        for t in tracks:
            for p in (t.GetStart(), t.GetEnd()):
                if len(ends[(p.x, p.y, t.GetLayer())]) > 1 or (p.x, p.y) in vias:
                    continue
                if any(pad.HitTest(p) and pad.IsOnLayer(t.GetLayer()) for pad in board.GetPads()):
                    continue
                dead.append(t)
                break
        if not dead:
            return removed
        for t in dead:
            board.Remove(t)
        removed += len(dead)


def unconnected(board):
    board.BuildConnectivity()
    return board.GetConnectivity().GetUnconnectedCount(False)


def finish():
    board = pcbnew.LoadBoard(PCB)
    ok = pcbnew.ImportSpecctraSES(board, SES)
    print("SES import:", ok)
    print("removed stubs:", remove_stubs(board))
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
    {"place": place, "finish": finish}[sys.argv[1]]()
