# pcbgen: schematic and PCB generated from one netlist

These scripts build the SAFE-NODE KiCad project in `hardware/kicad/` from a single source, `netlist.py`. The source lists 36 parts, each with its library symbol, value, footprint and pin-to-net map. Because the schematic and PCB come from the same data, they cannot drift apart.

| Script | What it does |
|---|---|
| `netlist.py` | Source of truth: parts, values, footprints, pin → net |
| `sexp.py` | Minimal S-expression parser and helpers for KiCad files |
| `gen_sch.py` | Writes `safe-node.kicad_sch`, the project symbol library and the library tables (A3 sheet, IEEE Std 315 class letters) |
| `gen_pcb.py` | Writes `safe-node.kicad_pro` and an unrouted `safe-node.kicad_pcb`: outline, placement, nets, silkscreen, the 1.0 mm RF track |
| `router.py` | Two-layer A* grid router (0.25 mm grid; clearance-inflated obstacles; 0.6 mm power and 0.3 mm signal tracks) |
| `finish_pcb.py` | Adds GND pours on both layers, fills the zones and writes the DRC report |
| `check_net.py` | Compares the KiCad-exported netlist with `netlist.py` |

## Requirements
- KiCad 7.0 or later. The `pcbnew` Python module must be importable, which is the case with KiCad's bundled Python on Linux.
- Python 3.10 or later, and `numpy`.

## Pipeline
```bash
cd tools/pcbgen
python3 gen_sch.py
kicad-cli sch export netlist -o ../../hardware/kicad/safe-node.net ../../hardware/kicad/safe-node.kicad_sch
python3 check_net.py          # expect: nets want 27 got 27 OK
python3 gen_pcb.py
python3 router.py
python3 finish_pcb.py
# Fabrication outputs
cd ../../hardware
kicad-cli pcb export gerbers --subtract-soldermask \
    -l F.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts -o fab/gerbers/ kicad/safe-node.kicad_pcb
kicad-cli pcb export drill --format excellon --excellon-separate-th --generate-map --map-format pdf -o fab/gerbers/ kicad/safe-node.kicad_pcb
kicad-cli pcb export pos --format csv --units mm -o fab/safe-node-pos.csv kicad/safe-node.kicad_pcb
kicad-cli sch export pdf -o plots/safe-node-schematic.pdf kicad/safe-node.kicad_sch
```

The generated files are identical from run to run, except for the random UUIDs.

**Warning:** these scripts overwrite `hardware/kicad/`. Once the board is edited by hand in KiCad, treat the KiCad files as the source and stop using the generator.
