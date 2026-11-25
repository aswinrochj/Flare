# Software and System Requirements Specification: SAFE-NODE

| | |
|---|---|
| Project | Fail-Safe Real-Time Gas and Fire Safety Network (SAFE-NODE) |
| Team | NEXUS · M2026-133 · St. Joseph's College of Engineering |
| Document | SRS, revision 1.0, 22 November 2026 |
| Structure | ISO/IEC/IEEE 29148:2018, system and software requirements |
| Related | DPR [1], Technical Documentation [2], Test Plan (`../test/test-plan.md`) |

## 1. Introduction

### 1.1 Purpose
This document lists the requirements that SAFE-NODE must meet. Each requirement has an ID and is traced to a DPR objective (O1–O6) and to the test that verifies it.

### 1.2 Scope
SAFE-NODE detects flammable gas, flame and abnormal heat in small industrial sites. It sounds a local alarm and reports to a control room over LoRa (primary) or ESP-NOW (secondary). It does this without relying on Wi-Fi or the internet. It also reports its own faults, so it never fails silently.

The scope excludes:
- certified gas-measurement accuracy;
- hazardous-area (Ex) compliance;
- long-term sensor drift.

### 1.3 Definitions
| Term | Meaning |
|---|---|
| Node | One ESP32 sensor unit on the SAFE-NODE carrier board |
| Gateway | ESP32 and SX1276 in the control room, with its own siren |
| Host | Raspberry Pi or PC running the MQTT bridge, dashboard and SMS queue |
| NORMAL / WARNING / DANGER / FAULT / OFFLINE | Node states (Technical Documentation Fig. 4) |
| Confirmed crossing | Three consecutive samples beyond a threshold |

### 1.4 Verification methods
**T** Test, **A** Analysis, **I** Inspection, **D** Demonstration.

## 2. Stakeholders
Workers in the zone, the control-room operator, maintenance staff, evaluators and developers. Their concerns are listed in Table II of the Technical Documentation.

## 3. Requirements

### 3.1 Functional: detection and annunciation
| ID | Requirement | Obj. | Verif. | Test |
|---|---|---|---|---|
| FR-01 | The node shall sample the MQ-2 and flame analog outputs at least every 100 ms. | O1 | T | TC-1, SIM-3 |
| FR-02 | The node shall read the DHT22 no more often than every 2 s. | O5 | T, I | TC-5 |
| FR-03 | The node shall enter WARNING at 10 % LEL equivalent and DANGER on a confirmed crossing of the danger threshold. Both thresholds shall have hysteresis. | O1 | T | TC-1, SIM-2 |
| FR-04 | DANGER shall latch until an operator acknowledges it and the reading has returned below the clear threshold. | O1 | T | TC-1 |
| FR-05 | In DANGER the node shall drive the red LED and the buzzer. In NORMAL it shall show the green LED only, and in FAULT the yellow LED with the buzzer off. | O1 | T | SIM-2, SIM-5, SIM-6 |
| FR-06 | A hazard indication shall take priority over a fault indication on the local outputs. | O1 | T, I | SIM-2 |

### 3.2 Functional: self-diagnosis (fail-safe)
| ID | Requirement | Obj. | Verif. | Test |
|---|---|---|---|---|
| FR-10 | At power-on the node shall test each sensor input, the DHT22 read, the LEDs and the buzzer. It shall report OK only for checks that actually ran and passed. | O5 | T | SIM-1 (AR-2) |
| FR-11 | An open or railed gas input shall be reported as FAULT within 2 s and shall never be reported as NORMAL. | O5 | T | TC-5 |
| FR-12 | A stuck sensor (no variation outside the plausibility band) shall be reported as FAULT within 60 s. | O5 | T | TC-5 |
| FR-13 | A DHT22 NaN or timeout shall set the temperature-sensor fault. | O5 | T | TC-5 (AR-3) |
| FR-14 | The task watchdog shall reset the node within 5 s of a hang in any safety task. It shall be fed only when every safety task has checked in. | O4 | T | TC-4 |
| FR-15 | The cause of each reset shall be logged and reported after reboot. | O4 | T | TC-4 |
| FR-16 | The node shall warn of low supply (below 3.4 V) before a brown-out reset. | O6 | T | TC-8 |

### 3.3 Functional: communication and supervision
| ID | Requirement | Obj. | Verif. | Test |
|---|---|---|---|---|
| FR-20 | Each node shall send a heartbeat every 3 s that carries its real state, not a fixed "OK". | O3 | T, I | TC-3 (AR-6) |
| FR-21 | The gateway shall flag a node OFFLINE after 3 missed heartbeats, within 10 s of the node stopping. | O3 | T | TC-3 |
| FR-22 | Alerts shall be acknowledged and retried. Packet delivery shall be at least 99 % at 10, 30 and 60 m through walls. | O2 | T | TC-9 |
| FR-23 | The system shall deliver alerts with Wi-Fi and the internet unavailable. | O2 | T | TC-2 |
| FR-24 | The gateway shall sound a system-fault tone within 3 s if the host bridge stops. | O2 | T | TC-7 |
| FR-25 | The host shall show node state on a dashboard and queue SMS alerts with store-and-forward. | O2 | D | TC-1 |

### 3.4 Performance
| ID | Requirement | Obj. | Verif. | Test |
|---|---|---|---|---|
| PR-01 | Local siren latency ≤ 0.5 s from a confirmed threshold crossing, excluding sensor chemical response. | O1 | T, A | TC-1 |
| PR-02 | Control-room siren ≤ 1 s and dashboard ≤ 2 s after the crossing. | O2 | T, A | TC-1, TC-2 |
| PR-03 | Alarm latency shall not change by more than 1 ms when lower-priority tasks use 100 % of idle time. | O6 | T | TC-6 |
| PR-04 | Battery backup of at least 8 h at normal-operation load. | O6 | A, T | TC-8 |

### 3.5 Hardware and interface
| ID | Requirement | Obj. | Verif. | Test |
|---|---|---|---|---|
| HR-01 | No ESP32 input shall see more than 3.3 V. The MQ-2 output shall be divided to ≤ 2.45 V at 5.1 V. | — | I, A | Schematic (AR-1) |
| HR-02 | The MQ-2 heater shall receive 5.0 ± 0.1 V. | — | T | Bring-up step 5 |
| HR-03 | The DevKitC shall be powered from one source at a time (JP1). | — | I | Hardware README |
| HR-04 | The LoRa radio shall operate only in 865–867 MHz. | — | I, T | TC-9 |
| HR-05 | The board shall pass KiCad DRC with 0 unconnected pads and 0 clearance errors. | — | A | `hardware/reports/drc.rpt` |
| HR-06 | Node bill of materials ≤ ₹2,500. | O6 | A | BOM |

### 3.6 Constraints
- The node uses an ESP32 (ESP-IDF v5 and FreeRTOS for v1.0) with an SX1276 radio.
- The system is a prototype and must not be presented as a certified detector.

## 4. Traceability summary

| Objective | Requirements | Tests |
|---|---|---|
| O1 local alarm | FR-01, FR-03–FR-06, PR-01 | TC-1, SIM-2, SIM-3, SIM-5 |
| O2 remote alert | FR-22–FR-25, PR-02 | TC-1, TC-2, TC-7, TC-9 |
| O3 offline detection | FR-20, FR-21 | TC-3 |
| O4 hang recovery | FR-14, FR-15 | TC-4 |
| O5 sensor faults | FR-02, FR-10–FR-13 | TC-5, SIM-1, SIM-6 |
| O6 cost, backup, isolation | FR-16, PR-03, PR-04, HR-06 | TC-6, TC-8, BOM |

## 5. References
[1] Team NEXUS, "Fail-safe real-time gas and fire safety network," DPR, M2026-133, Nov. 2026.
[2] Team NEXUS, "SAFE-NODE technical documentation," rev. 1.1, Nov. 2026.
[3] *Systems and software engineering — Life cycle processes — Requirements engineering*, ISO/IEC/IEEE 29148:2018.
