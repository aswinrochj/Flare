"""Small two-layer grid router (A*) for the SAFE-NODE carrier board.
Uses only the KiCad pcbnew API and numpy. Obstacles are inflated by clearance + half track width + a
discretisation margin, so the result is checked afterwards with KiCad's own DRC."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
KICAD = os.path.normpath(os.path.join(HERE, '..', '..', 'hardware', 'kicad'))
import pcbnew, numpy as np, heapq, math, sys

PCB = os.path.join(KICAD, "safe-node.kicad_pcb")
OX, OY, BW, BH = 50.0, 40.0, 100.0, 75.0
G = 0.25                       # grid pitch (mm)
NX, NY = int(BW / G) + 1, int(BH / G) + 1
CLR = 0.2; MARGIN = 0.12; EDGE = 0.4
VIA_D, VIA_DR = 0.8, 0.4
WIDTH = {"Power": 0.6, "Default": 0.3}
POWER = {"+5V", "VIN", "+3V3", "BUZ_N", "DEVKIT_5V", "GND"}
mm = pcbnew.FromMM; tomm = pcbnew.ToMM

b = pcbnew.LoadBoard(PCB)
xs = OX + np.arange(NX) * G; ys = OY + np.arange(NY) * G
XX, YY = np.meshgrid(xs, ys, indexing="ij")

# obstacle primitives: (netname, layerset{0,1}, kind, params)   layer 0 = F.Cu, 1 = B.Cu
prims = []
for fp in b.GetFootprints():
    for p in fp.Pads():
        layers = set()
        if p.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
            layers = {0, 1}
        else:
            if p.IsOnLayer(pcbnew.F_Cu): layers.add(0)
            if p.IsOnLayer(pcbnew.B_Cu): layers.add(1)
        pos = p.GetPosition(); cx, cy = tomm(pos.x), tomm(pos.y)
        if p.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
            r = tomm(p.GetDrillSize().x) / 2
            prims.append(("", layers, "circle", (cx, cy, r)))
        elif p.GetShape() == pcbnew.PAD_SHAPE_CIRCLE:
            prims.append((p.GetNetname(), layers, "circle", (cx, cy, tomm(p.GetSize().x) / 2)))
        else:
            bb = p.GetBoundingBox()
            prims.append((p.GetNetname(), layers, "rect", (tomm(bb.GetLeft()), tomm(bb.GetTop()), tomm(bb.GetRight()), tomm(bb.GetBottom()))))
for t in b.GetTracks():
    if t.GetClass() == "PCB_TRACK":
        L = 0 if t.GetLayer() == pcbnew.F_Cu else 1
        prims.append((t.GetNetname(), {L}, "seg", (tomm(t.GetStart().x), tomm(t.GetStart().y), tomm(t.GetEnd().x), tomm(t.GetEnd().y), tomm(t.GetWidth()) / 2)))

def mask_for(kind, prm, infl):
    if kind == "circle":
        cx, cy, r = prm; R = r + infl
        i0, i1 = max(0, int((cx - R - OX) / G)), min(NX, int((cx + R - OX) / G) + 2)
        j0, j1 = max(0, int((cy - R - OY) / G)), min(NY, int((cy + R - OY) / G) + 2)
        sub = (XX[i0:i1, j0:j1] - cx) ** 2 + (YY[i0:i1, j0:j1] - cy) ** 2 <= R * R
        return (i0, i1, j0, j1, sub)
    if kind == "rect":
        x0, y0, x1, y1 = prm
        i0, i1 = max(0, int((x0 - infl - OX) / G)), min(NX, int((x1 + infl - OX) / G) + 2)
        j0, j1 = max(0, int((y0 - infl - OY) / G)), min(NY, int((y1 + infl - OY) / G) + 2)
        X = XX[i0:i1, j0:j1]; Y = YY[i0:i1, j0:j1]
        dx = np.maximum(np.maximum(x0 - X, X - x1), 0); dy = np.maximum(np.maximum(y0 - Y, Y - y1), 0)
        return (i0, i1, j0, j1, dx * dx + dy * dy <= infl * infl)
    if kind == "seg":
        x0, y0, x1, y1, hw = prm; R = hw + infl
        i0, i1 = max(0, int((min(x0, x1) - R - OX) / G)), min(NX, int((max(x0, x1) + R - OX) / G) + 2)
        j0, j1 = max(0, int((min(y0, y1) - R - OY) / G)), min(NY, int((max(y0, y1) + R - OY) / G) + 2)
        X = XX[i0:i1, j0:j1]; Y = YY[i0:i1, j0:j1]
        vx, vy = x1 - x0, y1 - y0; L2 = vx * vx + vy * vy or 1e-9
        t = np.clip(((X - x0) * vx + (Y - y0) * vy) / L2, 0, 1)
        d2 = (X - (x0 + t * vx)) ** 2 + (Y - (y0 + t * vy)) ** 2
        return (i0, i1, j0, j1, d2 <= R * R)

def blocked_map(netname, hw):
    """cells where the centre of a track of half-width hw (on each layer) or a via may not go"""
    blk = np.zeros((2, NX, NY), bool); vblk = np.zeros((NX, NY), bool)
    e = EDGE + hw
    for L in (0, 1):
        blk[L, :int(e / G) + 1, :] = True; blk[L, -int(e / G) - 1:, :] = True
        blk[L, :, :int(e / G) + 1] = True; blk[L, :, -int(e / G) - 1:] = True
    vblk |= blk[0] | blk[1]
    for (n, layers, kind, prm) in prims:
        if n == netname and n != "": continue
        m = mask_for(kind, prm, CLR + hw + MARGIN)
        mv = mask_for(kind, prm, CLR + VIA_D / 2 + MARGIN)
        for L in layers:
            i0, i1, j0, j1, sub = m; blk[L, i0:i1, j0:j1] |= sub
        i0, i1, j0, j1, sub = mv; vblk[i0:i1, j0:j1] |= sub
    return blk, vblk

def cell(x, y): return int(round((x - OX) / G)), int(round((y - OY) / G))
def pt(i, j): return OX + i * G, OY + j * G

DIRS = [(1, 0, 1.0), (-1, 0, 1.0), (0, 1, 1.0), (0, -1, 1.0), (1, 1, 1.414), (1, -1, 1.414), (-1, 1, 1.414), (-1, -1, 1.414)]
VIA_COST = 12.0
def astar(starts, targets, blk, vblk, pref):
    """starts/targets: sets of (L,i,j). returns path list of (L,i,j)"""
    tl = np.array([(i, j) for (_, i, j) in targets]);
    def h(i, j):
        return float(np.min(np.abs(tl[:, 0] - i) + np.abs(tl[:, 1] - j))) * 0.9 if len(tl) < 64 else 0.0
    openh = []; g = {}; came = {}
    for s in starts:
        g[s] = 0.0; heapq.heappush(openh, (h(s[1], s[2]), 0.0, s))
    tset = set(targets); n = 0
    while openh:
        f, gc, cur = heapq.heappop(openh)
        if gc > g.get(cur, 1e18): continue
        if cur in tset:
            path = [cur]
            while path[-1] in came: path.append(came[path[-1]])
            return path[::-1]
        n += 1
        if n > 400000: return None
        L, i, j = cur
        for di, dj, c in DIRS:
            ni, nj = i + di, j + dj
            if not (0 <= ni < NX and 0 <= nj < NY): continue
            if blk[L, ni, nj] and (L, ni, nj) not in tset: continue
            if di and dj and (blk[L, i + di, j] or blk[L, i, j + dj]): continue
            # layer preference: F.Cu horizontal, B.Cu vertical
            pen = 0.0
            if (L == 0 and di == 0) or (L == 1 and dj == 0): pen = 0.35
            ng = gc + c + pen
            nb = (L, ni, nj)
            if ng < g.get(nb, 1e18):
                g[nb] = ng; came[nb] = cur; heapq.heappush(openh, (ng + h(ni, nj), ng, nb))
        # via
        if not vblk[i, j]:
            nb = (1 - L, i, j); ng = gc + VIA_COST
            if not blk[1 - L, i, j] and ng < g.get(nb, 1e18):
                g[nb] = ng; came[nb] = cur; heapq.heappush(openh, (ng + h(i, j), ng, nb))
    return None

def pad_cells(p):
    pos = p.GetPosition(); i, j = cell(tomm(pos.x), tomm(pos.y))
    layers = [L for L, lay in ((0, pcbnew.F_Cu), (1, pcbnew.B_Cu)) if p.IsOnLayer(lay)]
    return {(L, i, j) for L in layers}, (tomm(pos.x), tomm(pos.y))

def add_track(net, L, x0, y0, x1, y1, w):
    t = pcbnew.PCB_TRACK(b); t.SetStart(pcbnew.VECTOR2I(mm(x0), mm(y0))); t.SetEnd(pcbnew.VECTOR2I(mm(x1), mm(y1)))
    t.SetWidth(mm(w)); t.SetLayer(pcbnew.F_Cu if L == 0 else pcbnew.B_Cu); t.SetNet(net); b.Add(t)
    prims.append((net.GetNetname(), {L}, "seg", (x0, y0, x1, y1, w / 2)))
def add_via(net, x, y):
    v = pcbnew.PCB_VIA(b); v.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y))); v.SetDrill(mm(VIA_DR)); v.SetWidth(mm(VIA_D))
    v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); v.SetNet(net); b.Add(v)
    prims.append((net.GetNetname(), {0, 1}, "circle", (x, y, VIA_D / 2)))

def emit(net, path, w, pad_xy):
    pts = [(L, *pt(i, j)) for (L, i, j) in path]
    # snap endpoints to real pad centres when the path starts/ends on a pad cell
    segs = []; cur = [pts[0]]
    for k in range(1, len(pts)):
        a = pts[k]
        if a[0] != cur[-1][0]:
            segs.append(cur); add_via(net, a[1], a[2]); cur = [a]; continue
        cur.append(a)
    segs.append(cur)
    for s in segs:
        # merge collinear
        simp = [s[0]]
        for k in range(1, len(s) - 1):
            x0, y0 = simp[-1][1], simp[-1][2]; x1, y1 = s[k][1], s[k][2]; x2, y2 = s[k + 1][1], s[k + 1][2]
            if abs((x1 - x0) * (y2 - y1) - (y1 - y0) * (x2 - x1)) > 1e-9: simp.append(s[k])
        if len(s) > 1: simp.append(s[-1])
        for k in range(len(simp) - 1):
            add_track(net, simp[k][0], simp[k][1], simp[k][2], simp[k + 1][1], simp[k + 1][2], w)
    # stubs from grid to true pad centre
    for (L, x, y) in (pts[0], pts[-1]):
        for (px, py, layers) in pad_xy:
            if L in layers and abs(px - x) <= G and abs(py - y) <= G and (abs(px - x) > 1e-6 or abs(py - y) > 1e-6):
                add_track(net, L, px, py, x, y, w)

def route_net(name, skip_connected=True):
    net = b.FindNet(name); w = WIDTH["Power" if name in POWER else "Default"]
    pads = [p for fp in b.GetFootprints() for p in fp.Pads() if p.GetNetname() == name]
    if len(pads) < 2: return True
    pad_xy = [(tomm(p.GetPosition().x), tomm(p.GetPosition().y), {L for L, lay in ((0, pcbnew.F_Cu), (1, pcbnew.B_Cu)) if p.IsOnLayer(lay)}) for p in pads]
    blk, vblk = blocked_map(name, w / 2)
    groups = [pad_cells(p)[0] for p in pads]
    # order by distance from first pad (simple Prim-like)
    tree = set(groups[0]); remaining = groups[1:]
    while remaining:
        # pick nearest remaining group
        tl = [(i, j) for (_, i, j) in tree]
        def dist(gr):
            (_, i, j) = next(iter(gr)); return min(abs(i - a) + abs(j - c) for a, c in tl)
        remaining.sort(key=dist); gr = remaining.pop(0)
        path = astar(gr, tree, blk, vblk, None)
        if path is None:
            print("  FAILED segment of", name); return False
        emit(net, path, w, pad_xy)
        tree |= set(path) | gr
        blk, vblk = blocked_map(name, w / 2)   # refresh with own new copper harmless (same net skipped)
    return True

if __name__ == "__main__":
    order = sys.argv[1:] or []
    if not order:
        names = [n for n in b.GetNetsByName().keys() if str(n) and str(n) not in ("GND", "ANT")]
        def span(n):
            ps = [p.GetPosition() for fp in b.GetFootprints() for p in fp.Pads() if p.GetNetname() == n]
            if not ps: return 0
            xs_ = [tomm(q.x) for q in ps]; ys_ = [tomm(q.y) for q in ps]
            return (max(xs_) - min(xs_)) + (max(ys_) - min(ys_))
        names = [str(n) for n in names]
        order = sorted([n for n in names if n in POWER], key=span) + sorted([n for n in names if n not in POWER], key=span)
    fails = []
    for n in order:
        ok = route_net(n); print(("ok  " if ok else "FAIL"), n);
        if not ok: fails.append(n)
    pcbnew.SaveBoard(PCB, b)
    print("failed:", fails)
