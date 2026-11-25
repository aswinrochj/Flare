# Test scripts

Scripts for evaluating the bench tests of DPR Table VIII. Record the firmware commit hash in every log (see [`../logs/TEMPLATE.md`](../logs/TEMPLATE.md)).

| Script | Test cases | Input |
|---|---|---|
| `edge_latency.py` | TC-1, TC-2, TC-6 | Logic-analyser CSV: node probe GPIO25 (DANGER confirmed) and buzzer GPIO13, or the gateway siren. Prints mean, p95 and max, and compares the max with the objective |
| `pdr.py` | TC-3, TC-9 | Host raw log (`python -m safenode_host … --raw-log run.log`). Prints PDR per node and the OFFLINE detection times |

Analytical figures for comparison come from `python3 tools/lora_budget.py`, which reproduces DPR Table X.

Failure injection on the node uses the debug console (see [`firmware/node/v1.0/README.md`](../../firmware/node/v1.0/README.md)):

| TC | Console command on the node |
|---|---|
| TC-3 | `mute` (or unplug the node) |
| TC-4 | `hang sensor` / `hang alarm` |
| TC-5 | Disconnect the MQ-2 AO wire, or `fault gas` |
| TC-6 | `flood 60` while repeating TC-1 |
