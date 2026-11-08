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
    board.Add(s)


def text(board, layer, s, x, y, size=0.8, angle=0, bold=False):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s)
    t.SetLayer(layer)
    t.SetPosition(mm(x, y))
    t.SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(size), pcbnew.FromMM(size)))
    t.SetTextThickness(pcbnew.FromMM(max(0.12, size * 0.15 * (1.4 if bold else 1.0))))
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


def keepout(board, x1, y1, x2, y2):
    """No tracks or vias near the optical holes (light path and router safety)."""
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True)
    z.SetLayerSet(pcbnew.LSET.AllCuMask())
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


def silk(board):
    F, B = pcbnew.F_SilkS, pcbnew.B_SilkS
    sy = D.SPLIT_Y
    lx, ly = D.PARTS["D1"][6][0], D.PARTS["D1"][6][1]
    q2 = D.PARTS["Q2"][6]
    q3 = D.PARTS["Q3"][6]
    for layer in (F, B):
        circle(board, layer, lx, ly, 2.7)               # LED spot: put this on the camera lens
        seg(board, layer, 1.3, sy, 1.9, sy, 0.2)          # split line, both sides of the LED
        seg(board, layer, 7.1, sy, 9.2, sy, 0.2)
        seg(board, layer, 9.2, sy - 0.6, 9.2, sy + 0.6, 0.2)
        text(board, layer, "A", q2[0], q2[1] - 2.6, 1.0, bold=True)
        text(board, layer, "B", q3[0], q3[1] + 2.6, 1.0, bold=True)
    # Top: name and short instructions, logo area between the MCU and USB-C.
    text(board, F, "AVsync", 41.3, 11.0, 2.2, angle=90, bold=True)
    text(board, F, "calibrator", 44.4, 11.0, 1.0, angle=90)
    text(board, F, "v0.1", 21.8, 11.0, 0.7, angle=90)
    # Bottom: how to use it (this side touches the phone or the screen).
    text(board, B, "LED on camera lens", 20.0, 2.0, 0.8)
    text(board, B, "A|B line on pattern edge", 21.0, 4.0, 0.8)
    text(board, B, "AVsync calibrator v0.1", 42.0, 11.0, 1.2)
    text(board, B, "not a toy", 42.0, 13.6, 0.7)
    for i, lab in enumerate(["5V", "GND", "DIO", "CLK", "RST"]):
        text(board, B, lab, 24.0 + 2.54 * i, 16.7, 0.6)


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

    for ref, (lib_id, value, fpid, lcsc, netmap, _sch, (x, y, rot, side)) in D.PARTS.items():
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
    zone(board, nets["GND"], pcbnew.F_Cu)
    zone(board, nets["GND"], pcbnew.B_Cu)
    # keep tracks and vias off the optical holes (LED cut-out and sensor holes)
    lx, ly = D.PARTS["D1"][6][:2]
    keepout(board, lx - 1.35, ly - 1.15, lx + 1.35, ly + 1.15)
    for q in ("Q2", "Q3"):
        qx, qy = D.PARTS[q][6][:2]
        keepout(board, qx - 1.45, qy - 1.45, qx + 1.45, qy + 1.45)
    silk(board)
    pcbnew.SaveBoard(PCB, board)
    ok = pcbnew.ExportSpecctraDSN(board, DSN)
    print("placed, DSN export:", ok)


def add_via(board, net, x, y):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(mm(x, y))
    v.SetWidth(pcbnew.FromMM(0.6))
    v.SetDrill(pcbnew.FromMM(0.3))
    v.SetNet(net)
    board.Add(v)


# Areas kept free of stitching vias so the silkscreen text stays whole (x1, y1, x2, y2).
NO_STITCH = [(37.0, 0.0, 46.5, 22.0), (12.0, 0.0, 31.0, 5.2), (22.0, 15.6, 36.0, 17.6)]


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


def unconnected(board):
    board.BuildConnectivity()
    return board.GetConnectivity().GetUnconnectedCount(False)


def finish():
    board = pcbnew.LoadBoard(PCB)
    ok = pcbnew.ImportSpecctraSES(board, SES)
    print("SES import:", ok)
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
