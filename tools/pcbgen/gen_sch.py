import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
KICAD = os.path.normpath(os.path.join(HERE, '..', '..', 'hardware', 'kicad'))
import uuid, math, os, json
from sexp import parse, get_symbol, pins, find
from netlist import PARTS, POWER_NETS, FLAG_NETS

OUT = KICAD
os.makedirs(OUT, exist_ok=True)
U = lambda: str(uuid.uuid4())
ROOT = U()
G = 1.27

def ser(x):
    if isinstance(x, list): return "(" + " ".join(ser(e) for e in x) + ")"
    return x

# ---------------- custom DevKitC symbol ----------------
from netlist import devkit_left, devkit_right
def pin_sx(typ, x, y, ang, name, num):
    return (f'(pin {typ} line (at {x} {y} {ang}) (length 5.08) (name "{name}" (effects (font (size 1.27 1.27)))) '
            f'(number "{num}" (effects (font (size 1.27 1.27)))))')
def ptype(n):
    if n == "3V3": return "power_out"
    if n == "GND": return "power_in"
    if n == "5V": return "passive"
    return "bidirectional"
pl = [pin_sx(ptype(n), -17.78, round(22.86 - i * 2.54, 2), 0, n, i + 1) for i, n in enumerate(devkit_left)]
pr = [pin_sx(ptype(n), 17.78, round(22.86 - i * 2.54, 2), 180, n, i + 20) for i, n in enumerate(devkit_right)]
DEVKIT_BODY = ('(symbol "ESP32-DevKitC-V4_0_1" (rectangle (start -12.7 25.4) (end 12.7 -25.4) '
               '(stroke (width 0.254) (type default)) (fill (type background))) '
               '(text "USB" (at 0 -22.86 0) (effects (font (size 1.27 1.27)))))'
               '(symbol "ESP32-DevKitC-V4_1_1" ' + " ".join(pl + pr) + ')')
def devkit_symbol(name):
    return (f'(symbol "{name}" (pin_names (offset 1.016)) (in_bom yes) (on_board yes) '
            '(property "Reference" "U" (at 0 27.94 0) (effects (font (size 1.27 1.27)))) '
            '(property "Value" "ESP32-DevKitC-V4" (at 0 -27.94 0) (effects (font (size 1.27 1.27)))) '
            '(property "Footprint" "SAFE-NODE:ESP32-DevKitC-V4_Socket" (at 0 0 0) (effects (font (size 1.27 1.27)) hide)) '
            '(property "Datasheet" "https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32/esp32-devkitc/user_guide.html" (at 0 0 0) (effects (font (size 1.27 1.27)) hide)) '
            + DEVKIT_BODY + ')')
open(f"{OUT}/safe-node.kicad_sym", "w").write("(kicad_symbol_lib (version 20220914) (generator safe_node_gen)\n  "
                                              + devkit_symbol("ESP32-DevKitC-V4") + "\n)\n")

# ---------------- library symbol cache ----------------
lib_cache, pin_cache = {}, {}
def load(lib_id):
    if lib_id in lib_cache: return
    lib, name = lib_id.split(":")
    if lib == "SAFE-NODE":
        txt = devkit_symbol(lib_id)
        sym = parse(txt)[0]
    else:
        sym = get_symbol(lib, name); sym = list(sym); sym[1] = f'"{lib_id}"'
    lib_cache[lib_id] = sym
    pin_cache[lib_id] = pins(sym)

def rot(vx, vy, r):
    c, s = round(math.cos(math.radians(r))), round(math.sin(math.radians(r)))
    return (vx * c + vy * s, -vx * s + vy * c)

def pin_world(lib_id, num, x, y, r):
    """returns list of (px, py, outward_dx, outward_dy) screen coords for pin number"""
    res = []
    for p in pin_cache[lib_id]:
        if p["num"] != num: continue
        vx, vy = rot(p["x"], -p["y"], r)
        a = math.radians(p["ang"])
        ox, oy = rot(-round(math.cos(a)), round(math.sin(a)), r)
        res.append((round(x + vx, 2), round(y + vy, 2), ox, oy))
    return res

items = []
def text(t, x, y, size=1.27, bold=False, just="left"):
    b = " bold" if bold else ""
    items.append(f'(text "{t}" (at {x} {y} 0) (effects (font (size {size} {size}){b}) (justify {just})) (uuid {U()}))')
def rect(x1, y1, x2, y2):
    items.append(f'(rectangle (start {x1} {y1}) (end {x2} {y2}) (stroke (width 0) (type dash)) (fill (type none)) (uuid {U()}))')
def wire(x1, y1, x2, y2):
    items.append(f'(wire (pts (xy {x1} {y1}) (xy {x2} {y2})) (stroke (width 0) (type default)) (uuid {U()}))')
def label(n, x, y, ox, oy):
    ang = {(1, 0): 0, (-1, 0): 180, (0, -1): 90, (0, 1): 270}[(ox, oy)]
    just = "left" if ang in (0, 90) else "right"
    items.append(f'(label "{n}" (at {x} {y} {ang}) (fields_autoplaced) (effects (font (size 1.27 1.27)) (justify {just} bottom)) (uuid {U()}))')
def noconn(x, y):
    items.append(f'(no_connect (at {x} {y}) (uuid {U()}))')

pwr_count = [0]
def place_symbol(ref, lib_id, value, fp, x, y, r, pinmap, ref_off=None, val_off=None, hide_val=False, power=False):
    load(lib_id)
    pins_here = sorted({p["num"] for p in pin_cache[lib_id]}, key=lambda s: (len(s), s))
    rx, ry = ref_off if ref_off else (2.54, -1.27)
    vx, vy = val_off if val_off else (2.54, 1.27)
    hide = " hide" if power and ref.startswith("#") else ""
    vh = " hide" if hide_val else ""
    fa = r if r in (90, 270) else 0
    jf = "left" if fa == 0 else "right"
    s = (f'(symbol (lib_id "{lib_id}") (at {x} {y} {r}) (unit 1) (in_bom {"no" if power else "yes"}) (on_board {"no" if power else "yes"}) (dnp no) (uuid {U()}) '
         f'(property "Reference" "{ref}" (at {x + rx} {y + ry} {fa}) (effects (font (size 1.27 1.27)) (justify {jf}){hide})) '
         f'(property "Value" "{value}" (at {x + vx} {y + vy} {fa}) (effects (font (size 1.27 1.27)) (justify {jf}){vh})) '
         f'(property "Footprint" "{fp}" (at {x} {y} 0) (effects (font (size 1.27 1.27)) hide)) '
         f'(property "Datasheet" "~" (at {x} {y} 0) (effects (font (size 1.27 1.27)) hide)) '
         + " ".join(f'(pin "{p}" (uuid {U()}))' for p in pins_here) +
         f' (instances (project "safe-node" (path "/{ROOT}" (reference "{ref}") (unit 1)))))')
    items.append(s)
    if power: return
    for p in pins_here:
        net = pinmap.get(p, "__absent__")
        for (px, py, ox, oy) in pin_world(lib_id, p, x, y, r):
            if net is None:
                noconn(px, py)
            elif net != "__absent__":
                PINREC.append(dict(ref=ref, pin=p, net=net, x=px, y=py, ox=ox, oy=oy))

PINREC = []
def connect_all():
    bynet = {}
    for rec in PINREC: bynet.setdefault(rec["net"], []).append(rec)
    for net, recs in bynet.items():
        if net not in POWER_NETS and len(recs) == 2:
            a, b = recs
            if a["y"] == b["y"] and a["oy"] == 0 and b["oy"] == 0 and a["ox"] == -b["ox"] and (b["x"] - a["x"]) * a["ox"] > 0:
                wire(a["x"], a["y"], b["x"], b["y"])
                mx = round((a["x"] + b["x"]) / 2 / 1.27) * 1.27
                items.append(f'(label "{net}" (at {mx:.2f} {a["y"]} 0) (effects (font (size 1 1)) (justify bottom)) (uuid {U()}))'); continue
            if a["x"] == b["x"] and a["ox"] == 0 and b["ox"] == 0 and a["oy"] == -b["oy"] and (b["y"] - a["y"]) * a["oy"] > 0:
                wire(a["x"], a["y"], b["x"], b["y"]); continue
        for rec in recs:
            px, py, ox, oy = rec["x"], rec["y"], rec["ox"], rec["oy"]
            L = 2.54
            ex, ey = round(px + ox * L, 2), round(py + oy * L, 2)
            wire(px, py, ex, ey)
            if net in POWER_NETS: pwr_symbol(net, ex, ey, ox, oy)
            else: label(net, ex, ey, ox, oy)

def pwr_symbol(net, x, y, ox, oy):
    pwr_count[0] += 1
    lib_id = POWER_NETS[net]
    if net == "GND": r = {(0, 1): 0, (0, -1): 180, (1, 0): 90, (-1, 0): 270}[(ox, oy)]
    else: r = {(0, -1): 0, (0, 1): 180, (-1, 0): 90, (1, 0): 270}[(ox, oy)]
    w = 1.0 * len(net) + 0.6
    if net == "GND": off = {0: (-1.5, 4.6), 180: (-1.5, -3.4), 90: (3.4, -0.6), 270: (-3.4 - w, -0.6)}[r]
    else: off = {0: (-1.7, -3.4), 180: (-1.7, 4.4), 90: (-3.6 - w, 0.5), 270: (3.6, 0.5)}[r]
    place_symbol(f"#PWR{pwr_count[0]:03d}", lib_id, net, "", x, y, r, None, val_off=off, power=True)

# ---------------- placement ----------------
P = PARTS
def put(ref, x, y, r=0, **kw):
    lib_id, val, fp, pm, _ = P[ref]
    place_symbol(ref, lib_id, val, fp, x, y, r, pm, **kw)

# Title / notes
text("SAFE-NODE sensor-node carrier board  -  Team NEXUS (M2026-133)", 25.4, 17.78, 2.54, True)
text("Simulation: https://app.labwired.com/p/963436e296210af24c3ec1ab57304f01  (final_slider_monitor)  and  https://app.labwired.com/p/dharshinir2028/b2b2b0950f8219b71e729534c77f84a7  (final)", 25.4, 22.86, 1.27)

# Block 1: power input
rect(22.86, 30.48, 106.68, 83.82); text("POWER INPUT", 25.4, 34.29, 1.8, True)
put("J5", 33.02, 55.88, 180, ref_off=(-2.0, -6.0), val_off=(-2.0, -4.2))
put("D5", 58.42, 55.88, 180, ref_off=(-2.5, -3.5), val_off=(-2.5, 3.8))
put("C1", 81.28, 60.96, 0, ref_off=(2.5, -1.5), val_off=(2.5, 1.5))
put("JP1", 81.28, 43.18, 0, ref_off=(-2.5, -3.8), val_off=(-2.5, 3.6))
text("JP1: fit only when the DevKit USB cable", 25.4, 73.66, 1.27)
text("is NOT connected (DevKitC accepts one supply only).", 25.4, 76.2, 1.27)
text("D5 protects against reversed 5 V input.", 25.4, 78.74, 1.27)

# PWR_FLAGs
def flag(net, x, y):
    pwr_count[0] += 1
    place_symbol(f"#FLG{pwr_count[0]:03d}", "power:PWR_FLAG", "PWR_FLAG", "", x, y, 0, None, val_off=(-9.5, -2.6), power=True)
    if net == "GND":
        wire(x, y, x, y + 2.54); pwr_symbol(net, x, y + 2.54, 0, 1)
    else:
        wire(x, y, x + 7.62, y); pwr_symbol(net, x + 7.62, y, 0, -1)
flag("+5V", 93.98, 50.8)
flag("GND", 96.52, 60.96)

# Block 2: ESP32 DevKitC
rect(129.54, 30.48, 238.76, 175.26); text("ESP32-DevKitC-V4 (U1)", 132.08, 34.29, 1.8, True)
put("U1", 184.15, 99.06, 0, ref_off=(-3.0, -29.5), val_off=(-9.0, 28.6))
text("Unused pins: no-connect. GPIO6-11 are SPI-flash pins - never use.", 132.08, 168.91, 1.27)
text("ADC inputs use ADC1 only (ADC2 is shared with Wi-Fi / ESP-NOW).", 132.08, 171.45, 1.27)

# Block 3: gas sensor
rect(22.86, 88.9, 106.68, 139.7); text("GAS SENSOR (MQ-2)", 25.4, 92.71, 1.8, True)
put("J2", 33.02, 116.84, 180, ref_off=(-2.0, -8.5), val_off=(-2.0, -6.7))
put("R1", 63.5, 111.76, 90, ref_off=(-2.5, -3.2), val_off=(-2.0, 3.6))
put("R2", 76.2, 121.92, 0, ref_off=(2.5, -1.3), val_off=(2.5, 1.3))
put("C4", 91.44, 121.92, 0, ref_off=(2.5, -1.3), val_off=(2.5, 1.3))
text("5 V full scale -> 2.27 V at ADC (accurate range 0.15-2.45 V).", 25.4, 134.62, 1.27)

# Block 4: flame + DHT22
rect(22.86, 144.78, 106.68, 205.74); text("FLAME + TEMPERATURE", 25.4, 148.59, 1.8, True)
put("J3", 33.02, 165.1, 180, ref_off=(-2.0, -8.5), val_off=(-2.0, -6.7))
put("C5", 63.5, 167.64, 0, ref_off=(2.5, -1.3), val_off=(2.5, 1.3))
put("J4", 33.02, 190.5, 180, ref_off=(-2.0, -7.2), val_off=(-2.0, -5.4))
put("R7", 76.2, 182.88, 0, ref_off=(2.5, -1.3), val_off=(2.5, 1.3))
put("C6", 91.44, 190.5, 0, ref_off=(2.5, -1.3), val_off=(2.5, 1.3))
text("DHT22: read no faster than every 2 s.", 25.4, 201.93, 1.27)

# Block 5: battery sense
rect(22.86, 210.82, 106.68, 256.54); text("BATTERY SENSE", 25.4, 214.63, 1.8, True)
put("J6", 33.02, 233.68, 180, ref_off=(-2.0, -5.5), val_off=(-2.0, -3.7))
put("R8", 63.5, 233.68, 90, ref_off=(-2.5, -3.2), val_off=(-2.0, 3.6))
put("R9", 76.2, 238.76, 0, ref_off=(2.5, -1.3), val_off=(2.5, 1.3))
put("C7", 91.44, 238.76, 0, ref_off=(2.5, -1.3), val_off=(2.5, 1.3))

# Block 6: LoRa
rect(259.08, 30.48, 396.24, 109.22); text("LORA RADIO (865-867 MHz)", 261.62, 34.29, 1.8, True)
put("U2", 309.88, 68.58, 0, ref_off=(4.5, -14.0), val_off=(4.5, -12.2))
put("J1", 353.06, 60.96, 0, ref_off=(-3.0, -4.0), val_off=(3.0, -4.0))
put("C2", 365.76, 81.28, 0, ref_off=(2.5, -1.3), val_off=(2.5, 1.3))
put("C3", 381.0, 81.28, 0, ref_off=(2.5, -1.3), val_off=(2.5, 1.3))
text("Keep ANT trace short (< 8 mm); 50 ohm CPWG preferred.", 261.62, 104.14, 1.27)

# Block 7: indicators & buzzer
rect(259.08, 114.3, 396.24, 205.74); text("STATUS LEDS + BUZZER", 261.62, 118.11, 1.8, True)
put("R3", 279.4, 134.62, 90, ref_off=(-2.5, -3.2), val_off=(-2.0, 3.6))
put("D1", 302.26, 134.62, 180, ref_off=(-3.0, -3.0), val_off=(-3.0, 4.3))
put("R4", 279.4, 149.86, 90, ref_off=(-2.5, -3.2), val_off=(-2.0, 3.6))
put("D2", 302.26, 149.86, 180, ref_off=(-3.0, -3.0), val_off=(-3.0, 4.3))
put("R5", 279.4, 165.1, 90, ref_off=(-2.5, -3.2), val_off=(-2.0, 3.6))
put("D3", 302.26, 165.1, 180, ref_off=(-3.0, -3.0), val_off=(-3.0, 4.3))
put("R6", 330.2, 180.34, 90, ref_off=(-2.5, -3.2), val_off=(-2.0, 3.6))
put("Q1", 350.52, 180.34, 0, ref_off=(5.0, -1.3), val_off=(5.0, 1.3))
put("BZ1", 368.3, 152.4, 0, ref_off=(3.0, -5.0), val_off=(3.0, 5.6))
put("D4", 383.54, 152.4, 270, ref_off=(3.0, -1.0), val_off=(3.0, 1.6))
text("Active 5 V buzzer; D4 clamps inductive kick.", 261.62, 201.93, 1.27)

# Block 8: mechanical
rect(259.08, 210.82, 396.24, 241.3); text("MECHANICAL", 261.62, 214.63, 1.8, True)
for i, ref in enumerate(["H1", "H2", "H3", "H4"]):
    put(ref, 274.32 + i * 20.32, 228.6, 0, ref_off=(-2.5, 3.8), val_off=(-2.5, 5.6))

connect_all()
# ---------------- write schematic ----------------
libs = "\n  ".join(ser(s) for s in lib_cache.values())
sch = f'''(kicad_sch (version 20230121) (generator eeschema)
  (uuid {ROOT})
  (paper "A3")
  (title_block (title "SAFE-NODE Sensor Node Carrier Board") (date "2026-11-22") (rev "1.0")
    (company "Team NEXUS - St. Joseph's College of Engineering, Chennai")
    (comment 1 "VConnecTech Engineering Marathon 2026 - Team ID M2026-133")
    (comment 2 "Simulation: app.labwired.com/p/963436e296210af24c3ec1ab57304f01")
    (comment 3 "Symbols: KiCad std library (IEC 60617 style); class letters per IEEE Std 315"))
  (lib_symbols
  {libs}
  )
  {chr(10).join(items)}
  (sheet_instances (path "/" (page "1")))
)
'''
open(f"{OUT}/safe-node.kicad_sch", "w").write(sch)
open(f"{OUT}/sym-lib-table", "w").write('(sym_lib_table\n  (lib (name "SAFE-NODE")(type "KiCad")(uri "${KIPRJMOD}/safe-node.kicad_sym")(options "")(descr "SAFE-NODE project symbols"))\n)\n')
open(f"{OUT}/fp-lib-table", "w").write('(fp_lib_table\n  (lib (name "SAFE-NODE")(type "KiCad")(uri "${KIPRJMOD}/safe-node.pretty")(options "")(descr "SAFE-NODE project footprints"))\n)\n')
print("schematic written,", len(items), "items")
