# Test plan: SAFE-NODE

| | |
|---|---|
| Identifier | TP-SAFE-NODE-1.0, 22 November 2026 |
| Format | IEEE Std 829-2008, master test plan |
| Requirements | [`../docs/SRS.md`](../docs/SRS.md) |

## 1. Test items
- Node hardware (carrier PCB rev 1.0)
- Firmware v0.1 in LabWired
- Firmware v1.0, gateway and host on the bench

## 2. Features to be tested
Hazard detection and annunciation, fault reporting, recovery, radio delivery, liveness and power.

## 3. Features not to be tested
Certified gas accuracy, hazardous-area compliance and long-term sensor drift.

## 4. Approach
Testing starts in simulation, to check logic and wiring (SIM-1 to SIM-6). Bench tests TC-1 to TC-9 then cover timing, radio and recovery.

- Latency is measured by toggling a spare GPIO at the threshold crossing and at the siren output, then capturing both edges on a logic analyser.
- End-to-end times use host timestamps synchronised by NTP.
- Faults are injected with debug UART commands.
- Each case runs 50 times. TC-9 runs 100 alerts at each distance.

## 5. Pass/fail criteria
A case passes only if the **worst case** over all runs meets the requirement. A single silent failure fails TC-3, TC-4 or TC-5.

## 6. Test cases
| ID | Input | Pass criterion | Requirements |
|---|---|---|---|
| TC-1 | Butane near the MQ-2 | Local siren ≤ 0.5 s; dashboard red ≤ 2 s | FR-01, FR-03–FR-05, PR-01, PR-02 |
| TC-2 | Wi-Fi and internet switched off | Control-room siren via LoRa ≤ 1 s | FR-23, PR-02 |
| TC-3 | Node power unplugged | OFFLINE ≤ 10 s | FR-20, FR-21 |
| TC-4 | Infinite loop injected in the sensor task | Watchdog reset and fault logged ≤ 5 s | FR-14, FR-15 |
| TC-5 | MQ-2 wire disconnected | FAULT ≤ 2 s, never NORMAL | FR-02, FR-11–FR-13 |
| TC-6 | Logger flooded to 100 % of idle time | TC-1 latency unchanged (±1 ms) | PR-03 |
| TC-7 | Host bridge process killed | Gateway system-fault tone ≤ 3 s | FR-24 |
| TC-8 | Supply lowered below 3.4 V | Low-power warning before reset | FR-16, PR-04 |
| TC-9 | Alerts at 10 / 30 / 60 m through walls | PDR ≥ 99 % with retries | FR-22, HR-04 |
| SIM-1…6 | See [`logs/2026-11-22_labwired_sim.md`](logs/2026-11-22_labwired_sim.md) | — | FR-05, FR-10 |

## 7. Environment
- Simulation: LabWired (see [`../simulation/README.md`](../simulation/README.md)).
- Bench: three nodes, one gateway, a Raspberry Pi 4 and a logic analyser.

## 8. Responsibilities
| Member | Area |
|---|---|
| Dharshini R | Firmware, test lead |
| Dheekshitha R | Hardware |
| Aswin Roch J | Radio and gateway |
| Anto Prisin D | Host and logs |

## 9. Deliverables
Dated test logs in `test/logs/` named `YYYY-MM-DD_<topic>.md`, started from [`logs/TEMPLATE.md`](logs/TEMPLATE.md) so that each log records the firmware commit hash. Captures are evaluated with the scripts in [`scripts/`](scripts/); analytical reference values come from `tools/lora_budget.py`. Anomalies are raised with the issue template **Anomaly report**.
