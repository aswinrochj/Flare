# Tools

| Tool | Purpose |
|---|---|
| [`lora_budget.py`](lora_budget.py) | Reproduces DPR Table X from equations (1)–(4): airtime, latency, offline bound, battery backup. `--check` fails if a figure drifts from the DPR (run in CI) |
| [`gen_vectors.py`](gen_vectors.py) | Writes `test/vectors/frames.txt` from the Python codec. The C unit test checks that the firmware encodes the same bytes |
| [`pcbgen/`](pcbgen/) | Generates the KiCad schematic and PCB from one netlist (see its README) |
