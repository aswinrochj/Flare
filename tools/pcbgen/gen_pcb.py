import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
KICAD = os.path.normpath(os.path.join(HERE, '..', '..', 'hardware', 'kicad'))
import pcbnew, json, os, sys
sys.path.insert(0, HERE)
from netlist import PARTS

D = KICAD
OX, OY = 50.0, 40.0          # board origin on the page (mm)
BW, BH = 100.0, 75.0         # board size (mm)
mm = pcbnew.FromMM
def P(x, y): return pcbnew.VECTOR2I(mm(OX + x), mm(OY + y))

# ---------------- project file with design rules and net classes ----------------
def nc(name, clr, tw, vd, vdr):
    return {"name": name, "clearance": clr, "track_width": tw, "via_diameter": vd, "via_drill": vdr,
            "bus_width": 12, "diff_pair_gap": 0.25, "diff_pair_via_gap": 0.25, "diff_pair_width": 0.2,
            "line_style": 0, "microvia_diameter": 0.3, "microvia_drill": 0.1, "pcb_color": "rgba(0, 0, 0, 0.000)",
            "schematic_color": "rgba(0, 0, 0, 0.000)", "wire_width": 6}
pro = {
    "board": {"design_settings": {
        "defaults": {"board_outline_line_width": 0.1, "copper_line_width": 0.2, "copper_text_size_h": 1.5,
                     "copper_text_size_v": 1.5, "copper_text_thickness": 0.3, "silk_line_width": 0.15,
                     "silk_text_size_h": 1.0, "silk_text_size_v": 1.0, "silk_text_thickness": 0.15},
        "rules": {"min_clearance": 0.2, "min_copper_edge_clearance": 0.3, "min_hole_clearance": 0.25,
                  "min_hole_to_hole": 0.25, "min_through_hole_diameter": 0.3, "min_track_width": 0.2,
                  "min_via_annular_width": 0.15, "min_via_diameter": 0.6, "min_silk_clearance": 0.0,
                  "min_text_height": 0.8, "min_text_thickness": 0.08, "solder_mask_to_copper_clearance": 0.0,
                  "use_height_for_length_calcs": True},
        "track_widths": [0.0, 0.3, 0.6, 1.0], "via_dimensions": [{"diameter": 0.0, "drill": 0.0}, {"diameter": 0.8, "drill": 0.4}],
        "rule_severities": {"silk_overlap": "ignore", "silk_over_copper": "warning", "silk_edge_clearance": "warning",
                            "lib_footprint_issues": "ignore", "lib_footprint_mismatch": "ignore",
                            "footprint_type_mismatch": "ignore", "courtyards_overlap": "error"}}},
    "net_settings": {"classes": [nc("Default", 0.2, 0.3, 0.8, 0.4), nc("Power", 0.25, 0.6, 1.0, 0.5), nc("RF", 0.3, 1.0, 0.8, 0.4)],
                     "meta": {"version": 3}, "net_colors": None, "netclass_assignments": None,
                     "netclass_patterns": [{"netclass": "Power", "pattern": p} for p in ["+5V", "VIN", "GND", "+3V3", "BUZ_N", "DEVKIT_5V"]]
                                          + [{"netclass": "RF", "pattern": "ANT"}]},
    "meta": {"filename": "safe-node.kicad_pro", "version": 1},
    "pcbnew": {"page_layout_descr_file": ""},
}
json.dump(pro, open(f"{D}/safe-node.kicad_pro", "w"), indent=2)

# ---------------- placement (local mm, rotation deg CCW) ----------------
PLACE = {
    "H1": (3.5, 3.5, 0), "H2": (96.5, 3.5, 0), "H3": (3.5, 71.5, 0), "H4": (96.5, 71.5, 0),
    "U1": (8.5, 10.0, 0),
    "U2": (80.0, 20.0, 0), "J1": (97.46, 27.0, 0), "C3": (91.5, 15.0, 90), "C2": (90.0, 8.0, 0),
    "D1": (42.0, 5.0, 0), "D2": (50.0, 5.0, 0), "D3": (58.0, 5.0, 0),
    "R3": (40.0, 12.0, 0), "R4": (40.0, 15.5, 0), "R5": (40.0, 19.0, 0),
    "BZ1": (50.0, 34.0, 0), "Q1": (62.5, 44.0, 0), "R6": (40.0, 46.0, 0), "D4": (50.5, 49.0, 0),
    "JP1": (40.0, 51.0, 90),
    "R2": (40.0, 58.5, 0), "C4": (50.0, 58.5, 0), "R1": (40.0, 62.0, 0),
    "C5": (51.0, 62.0, 0), "C6": (56.0, 58.5, 0), "R7": (57.0, 62.0, 0),
    "R9": (68.0, 58.5, 0), "R8": (68.0, 62.0, 0), "C7": (78.5, 62.0, 0),
    "D5": (82.0, 56.0, 0), "C1": (90.0, 48.0, 0),
    "J2": (39.0, 70.0, 90), "J3": (51.0, 70.0, 90), "J4": (63.0, 70.0, 90), "J6": (72.0, 70.0, 90),
    "J5": (80.0, 70.0, 0),
}
LIBDIR = "/usr/share/kicad/footprints"

board = pcbnew.BOARD()
nets = {}
def net(n):
    if n not in nets:
        ni = pcbnew.NETINFO_ITEM(board, n); board.Add(ni); nets[n] = ni
    return nets[n]

for ref, (lib_id, value, fp_id, pinmap, desc) in PARTS.items():
    lib, name = fp_id.split(":")
    path = f"{D}/safe-node.pretty" if lib == "SAFE-NODE" else f"{LIBDIR}/{lib}.pretty"
    fp = pcbnew.FootprintLoad(path, name)
    fp.SetReference(ref); fp.SetValue(value)
    fp.SetFPIDAsString(fp_id) if hasattr(fp, "SetFPIDAsString") else None
    x, y, r = PLACE[ref]
    fp.SetPosition(P(x, y)); fp.SetOrientationDegrees(r)
    board.Add(fp)
    for pad in fp.Pads():
        n = pinmap.get(pad.GetNumber())
        if n: pad.SetNet(net(n))
    fp.Value().SetVisible(False)
    if ref == "C3": fp.Reference().SetPosition(P(94.3, 11.5))
    if ref in ("R3", "R4", "R5"):
        fp.Reference().SetPosition(P(x - 1.6, y)); fp.Reference().SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_RIGHT)
    if ref in ("J2", "J3", "J4", "J6", "H1", "H2", "H3", "H4"): fp.Reference().SetVisible(False)

# ---------------- outline ----------------
def seg(x1, y1, x2, y2, layer=pcbnew.Edge_Cuts, w=0.1):
    s = pcbnew.PCB_SHAPE(board); s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(P(x1, y1)); s.SetEnd(P(x2, y2)); s.SetLayer(layer); s.SetWidth(mm(w)); board.Add(s)
for a in [(0, 0, BW, 0), (BW, 0, BW, BH), (BW, BH, 0, BH), (0, BH, 0, 0)]: seg(*a)

# ---------------- silkscreen text ----------------
def text(t, x, y, size=1.0, layer=pcbnew.F_SilkS, just=0, th=0.15):
    tx = pcbnew.PCB_TEXT(board); tx.SetText(t); tx.SetPosition(P(x, y)); tx.SetLayer(layer)
    tx.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size))); tx.SetTextThickness(mm(th))
    if just < 0: tx.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_LEFT)
    if just > 0: tx.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_RIGHT)
    board.Add(tx)
text("MQ-2", 42.8, 67.4, 0.8); text("5V GND DO AO", 42.8, 72.6, 0.8)
text("FLAME", 54.8, 67.4, 0.8); text("3V3 GND DO AO", 54.8, 72.6, 0.8)
text("DHT22", 65.5, 67.4, 0.8); text("3V3 DAT GND", 65.5, 72.6, 0.8)
text("VBAT", 73.3, 67.4, 0.8); text("+  GND", 73.3, 72.6, 0.8)
text("5V IN:  +    -", 82.6, 64.9, 0.8)
text("HAZ", 43.27, 9.3, 0.8); text("OK", 51.27, 9.3, 0.8); text("FLT", 59.27, 9.3, 0.8)
text("ANT", 97.0, 21.0, 0.8)
text("5V_LINK: fit only without USB", 47.5, 53.6, 0.8)
text("SAFE-NODE v1.0", 21.7, 64.0, 1.5, th=0.25)
text("Team NEXUS  M2026-133", 21.7, 66.6, 1.0)
text("St. Joseph's College of Engg., Chennai", 21.7, 68.8, 0.8)
text("VConnecTech Engineering Marathon 2026", 21.7, 70.8, 0.8)
text("SAFE-NODE carrier  rev 1.0  2026-11-22", 65.0, 53.0, 1.0, layer=pcbnew.B_SilkS)

u2 = board.FindFootprintByReference("U2"); j1 = board.FindFootprintByReference("J1")
pa = [p for p in u2.Pads() if p.GetNumber() == "9"][0].GetPosition(); pb = [p for p in j1.Pads() if p.GetNumber() == "1"][0].GetPosition()
t = pcbnew.PCB_TRACK(board); t.SetStart(pa); t.SetEnd(pb); t.SetWidth(mm(1.0)); t.SetLayer(pcbnew.F_Cu); t.SetNet(net("ANT")); board.Add(t)
ns = board.GetDesignSettings().m_NetSettings
def mknc(name, clr, tw, vd, vdr):
    c = pcbnew.NETCLASS(name); c.SetClearance(mm(clr)); c.SetTrackWidth(mm(tw)); c.SetViaDiameter(mm(vd)); c.SetViaDrill(mm(vdr))
    ns.m_NetClasses[name] = c; return c
NC = {"Power": mknc("Power", 0.25, 0.6, 1.0, 0.5), "RF": mknc("RF", 0.3, 1.0, 0.8, 0.4)}
for n, ni in nets.items():
    if n in ["+5V", "VIN", "GND", "+3V3", "BUZ_N", "DEVKIT_5V"]: ni.SetNetClass(NC["Power"])
    if n == "ANT": ni.SetNetClass(NC["RF"])
ds = board.GetDesignSettings(); ds.m_CopperEdgeClearance = mm(0.3); ds.m_TrackMinWidth = mm(0.2)
pcbnew.SaveBoard(f"{D}/safe-node.kicad_pcb", board)
BOARD = board
print("placed", len(PARTS), "footprints,", len(nets), "nets")
