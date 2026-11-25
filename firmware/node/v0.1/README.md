# Node firmware v0.1 (Arduino prototype)

`main/main.ino` is the Arduino prototype that ran in the LabWired simulation (project `final_slider_monitor`, https://app.labwired.com/p/963436e296210af24c3ec1ab57304f01).

> **Provenance.** The sketch in this folder was rebuilt from the behaviour recorded in Section IV-A and Tables VIII–IX of the technical documentation and from the 22 November 2026 simulation log. It has the same functions, UART commands and serial messages. To keep the exact simulated build in the repository, open the LabWired project, copy `src/main.ino` from its code editor over `main/main.ino`, and commit it with the export date.

## Build

1. In Arduino IDE 2, open Boards Manager and install *esp32* by Espressif.
2. Open `firmware/node/v0.1/main/main.ino` and select the board *ESP32 Dev Module*. Upload it.
3. Open the Serial Monitor at 115200 baud.

| Command | Effect |
|---|---|
| `G<n>` | Set gas value to n counts (overwritten at the next sample) |
| `F<n>` | Set the flame-detected flag to n |
| `T<n>` / `H<n>` | Set temperature (°C) / humidity (%) |
| `X` / `Y` / `Z` | Toggle the gas / flame / temperature fault flag |
| `R` | Reset all values and flags (prints `[RECOVERY]`) |
| `?` | Help |

## Known limitations

v0.1 keeps anomalies AR-2 to AR-10 (see `test/logs/2026-11-22_labwired_sim.md`), so it matches the simulation record. **v0.1 is not fail-safe.** Do not use it on real hardware except for bring-up. Firmware v1.0 (`../v1.0`) closes AR-2 to AR-7 and AR-10.

**Hardware note:** on PCB rev 1.0 the MQ-2 input is divided (×0.455). Recalibrate the 1800-count gas threshold before using this sketch on the board.
