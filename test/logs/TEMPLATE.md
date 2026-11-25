# Test log: <TC-n / topic>

Copy this file to `test/logs/YYYY-MM-DD_<topic>.md` for each session (IEEE Std 829 test log).

| | |
|---|---|
| Date and time | YYYY-MM-DD HH:MM–HH:MM IST |
| Tester(s) | |
| Test cases | TC-x … (DPR Table VIII), SIM-x |
| Node firmware | `git rev-parse --short HEAD` = `xxxxxxx`; version line printed at boot: `firmware …` |
| Gateway firmware | `xxxxxxx` |
| Host software | `xxxxxxx`, `python -m safenode_host --version` |
| Configuration | `firmware/node/v1.0/main/app_config.h` at the same commit; Kconfig: node ID, SF, flame polarity |
| Hardware | carrier PCB rev, node IDs, antenna, supply (V at J2 pin 1) |
| Environment | location, distance from gateway, walls, ambient temperature |
| Raw data | `run.log` (`--raw-log`), logic-analyser CSV, photos (attach or link) |

## Procedure deviations

None / describe.

## Results

| Case | Run | Input | Expected | Observed | Result |
|---|---|---|---|---|---|
| TC-1 | 1/50 | | Local siren ≤ 0.5 s | | |

Latency: `python3 test/scripts/edge_latency.py capture.csv --start 0 --end 1 --limit-ms 500`
PDR / offline: `python3 test/scripts/pdr.py run.log`

| Metric | Mean | p95 | Max | Objective | Pass |
|---|---|---|---|---|---|
| | | | | | |

## Incidents

Open an issue with the *Anomaly report* template for every failure and list it here (AR-n).
