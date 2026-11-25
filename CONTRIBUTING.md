# Contributing

## Branches and commits
- `main` always holds a reviewed state. Work on a branch named `feature/<topic>`, `fix/AR-<n>` or `hw/<topic>`.
- Commit messages use the imperative mood and give a short summary of 72 characters or fewer, for example `fw: add task watchdog with conditional feed (AR-7)`.
- Use one of these prefixes: `fw:`, `gw:`, `host:`, `hw:`, `docs:`, `test:`, `tools:`.

## Pull requests
Every pull request must:
1. reference the requirement (`FR-xx`, `PR-xx`, `HR-xx`) or anomaly report (`AR-xx`) it addresses;
2. update `CHANGELOG.md` under *Unreleased*;
3. for firmware, state which test cases (`TC-x` / `SIM-x`) were run, and add a dated log in `test/logs/`;
4. for hardware, attach a fresh KiCad DRC report in `hardware/reports/` and regenerate `hardware/fab/` and `hardware/plots/`.

## Anomalies
To report a defect, open an issue with the **Anomaly report** template. Severity is classified per IEEE Std 1044:
- High: could damage hardware or hide a hazard;
- Medium: weakens a claimed function;
- Low: quality issue.

## Before you push
Run `make test` (firmware unit tests and host tests). If you change the frame format, update both codecs and run `python3 tools/gen_vectors.py`. If you change a timing or threshold that the DPR quotes, run `python3 tools/lora_budget.py --check`. CI runs the same checks and builds both ESP-IDF projects.

## Code style
- C/C++: ESP-IDF style (4-space indent, `snake_case`). Every safety task must check in with the watchdog supervisor.
- Python: PEP 8. Format with `black`.

## Safety
Never test with an open flame or gas indoors without supervision and ventilation. Use butane from a lighter (unlit) at a distance, as in TC-1.
