# SAFE-NODE carrier board, rev 1.0: hardware documentation

The carrier PCB holds one sensor node: an ESP32-DevKitC V4 in sockets, an RFM95W (SX1276) LoRa module, connectors for the MQ-2, flame and DHT22 modules, a 5 V power input, battery sensing, status LEDs and a buzzer driver.

- Schematic, drawn in KiCad 7 with class letters per IEEE Std 315: [`plots/safe-node-schematic.pdf`](plots/safe-node-schematic.pdf)
- PCB layout, 2-layer, IPC-2221B clearances: [`kicad/safe-node.kicad_pcb`](kicad/safe-node.kicad_pcb)
- Design rationale (divider values, power budget, driver sizing): Section III of [`../docs/M2026-133_SAFE-NODE_Technical_Documentation.pdf`](../docs/M2026-133_SAFE-NODE_Technical_Documentation.pdf)

## Simulation links

The circuit on this board was first simulated in LabWired:

| Project | Link | Contents |
|---|---|---|
| `final` | https://app.labwired.com/p/dharshinir2028/b2b2b0950f8219b71e729534c77f84a7 | Complete node circuit: ESP32, MQ-2, flame, DHT22, buzzer driver, LEDs |
| `final_slider_monitor` | https://app.labwired.com/p/963436e296210af24c3ec1ab57304f01 | Same circuit, with slide potentiometers driving GPIO32/33. Firmware v0.1. Electrical check passed |

Changes from the simulated circuit to this PCB:

| Change | Reason |
|---|---|
| MQ-2 AO goes through R1 = 12 kΩ / R2 = 10 kΩ to GPIO32 | Closes AR-1. Without the divider, up to 5 V reaches a 3.3 V pin |
| Buzzer base resistor raised from 220 Ω to 1 kΩ | Closes AR-8. 2.6 mA base current still saturates the PN2222A |
| LEDs changed to red, green and yellow | Closes AR-8 |
| Added: RFM95W, SMA connector, 5 V input with D5 reverse-polarity protection, JP1, battery sense, decoupling and ADC filter capacitors | Not modelled in the simulation |

Because of the divider, the gas threshold of 1800 counts used in the simulation must be recalibrated for hardware.

## Files

| Path | Contents |
|---|---|
| `kicad/safe-node.kicad_pro` | KiCad 7 project. Open this file |
| `kicad/safe-node.kicad_sch` | Schematic, single A3 sheet |
| `kicad/safe-node.kicad_pcb` | PCB layout: 100 × 75 mm, 2 layers |
| `kicad/safe-node.kicad_sym`, `kicad/safe-node.pretty/` | Project symbol and footprint libraries. The tables reference them through `${KIPRJMOD}` |
| `kicad/safe-node.net` | Exported netlist: 36 parts, 27 nets |
| `fab/gerbers/` | Gerber X2 layers, Excellon PTH/NPTH drill files and drill maps, `.gbrjob` |
| `fab/safe-node-gerbers.zip` | Same files, zipped for upload to a PCB maker |
| `fab/safe-node-bom.csv` | Grouped bill of materials, with function and indicative INR cost for each line |
| `fab/safe-node-pos.csv` | Component placement file |
| `plots/` | Schematic PDF and PNG; PCB top, bottom and assembly renders (PNG, SVG, PDF) |
| `reports/drc.rpt` | KiCad design-rule-check report |

All other footprints come from the standard KiCad 7 libraries.

## Board data

| Item | Value |
|---|---|
| Size and stack-up | 100 × 75 mm, 2 layers, FR-4 1.6 mm, 1 oz copper |
| Clearance / edge clearance | 0.2 mm / 0.3 mm |
| Track width | 0.3 mm signal, 0.6 mm power, 1.0 mm RF (ANT) |
| Vias | 0.8 mm pad, 0.4 mm drill (18 in total) |
| Ground | GND pour on both layers, with thermal reliefs |
| Mounting | 4 × M3 (3.2 mm) holes at the corners |
| Routing | 400 track segments, 1.25 m of track |

## Design-rule check

DRC was run in KiCad 7 (`reports/drc.rpt`).

| Result | Count | Disposition |
|---|---|---|
| Unconnected pads | 0 | — |
| Clearance / short errors | 0 | — |
| Footprint errors | 0 | — |
| `copper_edge_clearance` | 5 | Accepted. J1 is an edge-mount SMA connector and must sit on the board edge |
| `silk_overlap` | 4 | Cosmetic. Reference text touches outline silkscreen; legible on the plots |
| `lib_footprint_issues` | 35 | Environment only. The DRC machine had no global KiCad footprint table. These warnings disappear on a normal KiCad install |

The schematic netlist was compared with the source netlist in `tools/pcbgen/netlist.py`: 27 of 27 nets matched, with 0 differences. The board was routed by the grid router in `tools/pcbgen/router.py`, and every result was then checked with KiCad's own DRC.

## Before you order or power the board

1. **Measure the DevKit row spacing.** The footprint assumes the two 1×19 header rows are 25.4 mm (1.0 in) apart, as on the Espressif ESP32-DevKitC V4. Some clone boards use 22.86 mm or 27.94 mm. Hold the board against a 1:1 print of `plots/pcb-top.pdf` before ordering.
2. **Set the 5 V supply to 5.4 V.** The MQ-2 heater needs 5.0 ± 0.1 V, and D5 (1N5819) drops 0.3–0.45 V. With an adjustable boost module (MT3608), set its output to about 5.4 V, then measure 4.9–5.1 V at J2 pin 1.
3. **Use one power source at a time.** The DevKitC must have only one supply. JP1 joins the board's 5 V rail to the DevKit 5V pin. **Remove JP1 whenever a USB cable is plugged into the DevKit.**
4. **Check the MQ-2 module pin order** against J2 (pin 1 VCC, 2 GND, 3 DO, 4 AO) before plugging it in. DO is not connected.
5. **Connect the antenna before transmitting.** Fit an 865–867 MHz antenna to J1 before running any LoRa firmware.
6. **Check flame-module polarity.** Some modules pull AO low when they see a flame. Confirm on the real module before setting the comparison direction in firmware (AR-9).

## Assembly order

1. Low parts first: resistors R1–R9, diodes D4 and D5 (check the band), and capacitors C3–C7.
2. Then the RFM95W (U2, SMD castellations, hand-solderable with a fine tip), followed by the SMA connector J1.
3. Then Q1 (check the flat face against the silkscreen), the LEDs D1–D3 (long lead = anode, square pad = cathode), the buzzer BZ1 (+ to the 5V pad), and electrolytics C1 and C2 (observe polarity).
4. Last, the two 1×19 female sockets for U1, the pin headers J2–J4, JP1, and the terminal blocks J5 and J6.
5. Before inserting the DevKit, power J5 and check: 5 V at JP1 pin 1; with JP1 fitted and the DevKit inserted, 3.3 V on the +3V3 net; no more than 2.3 V at GPIO32 with the MQ-2 fitted.

## Regenerating the design

The schematic and PCB are generated from one netlist (`tools/pcbgen/netlist.py`). See [`../tools/pcbgen/README.md`](../tools/pcbgen/README.md). If you edit them by hand in KiCad, stop using the generator, or your edits will be overwritten.

## Licence

The hardware design files in this directory are licensed under CERN-OHL-P-2.0 (see [`../LICENSES/CERN-OHL-P-2.0.txt`](../LICENSES/CERN-OHL-P-2.0.txt)).
