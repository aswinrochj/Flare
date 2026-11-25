import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
KICAD = os.path.normpath(os.path.join(HERE, '..', '..', 'hardware', 'kicad'))
import pcbnew, sys
PCB = os.path.join(KICAD, "safe-node.kicad_pcb")
OX, OY, BW, BH = 50.0, 40.0, 100.0, 75.0
mm = pcbnew.FromMM
b = pcbnew.LoadBoard(PCB)
gnd = b.FindNet("GND")
for z in list(b.Zones()): b.Remove(z)
for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
    z = pcbnew.ZONE(b)
    z.SetLayer(layer); z.SetNet(gnd)
    ol = z.Outline(); ol.NewOutline()
    for (x, y) in [(0, 0), (BW, 0), (BW, BH), (0, BH)]:
        ol.Append(mm(OX + x), mm(OY + y))
    z.SetLocalClearance(mm(0.3)); z.SetMinThickness(mm(0.25))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    z.SetThermalReliefGap(mm(0.4)); z.SetThermalReliefSpokeWidth(mm(0.5))
    z.SetIsFilled(False); z.SetZoneName("GND_" + ("TOP" if layer == pcbnew.F_Cu else "BOT"))
    z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    b.Add(z)
# stitching vias on a grid where both pours exist (added only if far from other copper)
extra = [tuple(map(float, a.split(","))) for a in sys.argv[1:]]
for (x, y) in extra:
    v = pcbnew.PCB_VIA(b); v.SetPosition(pcbnew.VECTOR2I(mm(OX + x), mm(OY + y))); v.SetDrill(mm(0.4)); v.SetWidth(mm(0.8))
    v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); v.SetNet(gnd); b.Add(v)
for ref, num in (("U1", "20"), ("J3", "2"), ("Q1", "1")):
    for p in b.FindFootprintByReference(ref).Pads():
        if p.GetNumber() == num: p.SetZoneConnection(pcbnew.ZONE_CONNECTION_FULL)
filler = pcbnew.ZONE_FILLER(b); filler.Fill(b.Zones())
pcbnew.SaveBoard(PCB, b)
b = pcbnew.LoadBoard(PCB)
pcbnew.WriteDRCReport(b, os.path.join(KICAD, "drc.rpt"), pcbnew.EDA_UNITS_MILLIMETRES, True)
print("done")
