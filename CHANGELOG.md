# Changelog

All notable changes are recorded here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]
### Planned
- Bench tests TC-1 to TC-9 on three nodes and one gateway; replace the analytical values of DPR Table X with measurements.
- Field commissioning: R0 calibration, flame-module polarity (AR-9), range trials at 10/30/60 m.

## [1.1.0] - 2026-11-25
### Added
- Node firmware v1.0 (ESP-IDF v5, FreeRTOS): sensor, alarm, health, radio, heartbeat, diag and logger tasks; 3 s task watchdog fed only when both safety tasks check in; boot self-test that reports real results; plausibility rules of DPR Table V; 3-sample confirmation, hysteresis, WARNING level and latched DANGER; DHT22 driver; reset cause and count in the first frame; NVS event ring; debug console with failure-injection hooks; latency probe on GPIO25. Addresses AR-2 to AR-7 and AR-10 (verification on hardware pending).
- Gateway firmware: node table, OFFLINE after 3 missed heartbeats, ACK and duplicate handling, beacon, control-room siren and fault tone, host PING/PONG, operator acknowledgement.
- Shared components: 20-byte frame codec with CRC-16/CCITT-FALSE, register-level SX1276 driver locked to 865–867 MHz, board-support header.
- Host software: serial bridge, SQLite history, MQTT topics, SMS queue with store-and-forward (Twilio or console), Flask floor-map dashboard, gateway emulator for running without hardware.
- Tests: firmware unit tests on a PC (protocol, classifier, node table), host tests including an end-to-end scenario, shared protocol vectors checked by both C and Python.
- Tools and scripts: `tools/lora_budget.py` reproduces DPR Table X; `test/scripts/edge_latency.py` and `pdr.py` evaluate bench captures; test-log template recording the firmware commit.
- GitHub Actions CI; `Makefile`; `host/requirements.txt`.
- Firmware v0.1 sketch (`firmware/node/v0.1/main/main.ino`), rebuilt from the documented behaviour of the LabWired build.

## [1.0.0] - 2026-11-22
### Added
- Detailed Project Report (DPR) for the VConnecTech Engineering Marathon 2026.
- Technical documentation rev 1.1, following IEEE Std 1016 for design and IEEE Std 829 for test.
- SRS following ISO/IEC/IEEE 29148, with traceability to objectives O1–O6.
- KiCad 7 schematic and 2-layer carrier PCB rev 1.0, with Gerbers, BOM, placement file and DRC report.
- LabWired simulation links and the 22 November 2026 simulation test log, with anomaly reports AR-1 to AR-12.
- Generator scripts that build the schematic and PCB from one netlist (`tools/pcbgen`).

### Fixed (hardware, relative to the simulated circuit)
- AR-1: MQ-2 output now passes through a 12 kΩ / 10 kΩ divider before GPIO32.
- AR-8: buzzer base resistor raised to 1 kΩ; LEDs changed to red, green and yellow.
