# Node firmware v1.0 (ESP-IDF v5 + FreeRTOS)

Target firmware for the SAFE-NODE sensor node. It implements the design in Sections IV–V of the DPR and Section IV-B of the technical documentation, and the functional requirements FR-01 to FR-22 of [`docs/SRS.md`](../../../docs/SRS.md).

> **Status (25 Nov 2026).** The code is complete and builds with ESP-IDF v5.2.3 with no errors or warnings (image 825 KB, 81 % of the 1 MB app partition). The decision logic passes its unit tests on a PC. It has **not yet run on hardware**; bench tests TC-1 to TC-9 are scheduled for the Implementation Round.

## Task set (DPR Table III)

| Task | Priority / core | Period / trigger | Job |
|---|---|---|---|
| sensor | 5, core 1 | 100 ms | Sample (16× oversampled gas, eFuse-calibrated), plausibility, classify, report |
| alarm | 5, core 1 | Notification | Red LED + buzzer, latched DANGER, LEDs |
| health | 4, core 1 | 500 ms | Feeds the 3 s task watchdog **only if** sensor and alarm both checked in |
| radio | 4, core 0 | Queue | LoRa TX/RX, CAD, ACK + 3 retries with 20–80 ms back-off; ESP-NOW fallback (Wi-Fi started only when LoRa delivery fails, to keep the power budget) |
| heartbeat | 3, core 0 | 3 s, own slot | Heartbeat carrying the real state (closes AR-6) |
| diag | 3, core 0 | 2 s | DHT22 read; NaN, timeout or checksum error sets the temperature fault (closes AR-3) |
| logger | 1, core 0 | Idle | NVS event ring, status line once per second (closes AR-10), UART console |

Tasks share no mutable globals. Data moves only through a direct-to-task notification (sensor → alarm), an 8-deep queue (→ radio), one-slot mailboxes (status, DHT22, beacon time) and an event group (alive bits).

## Decision rules (DPR Table V)

All thresholds and timings live in [`main/app_config.h`](main/app_config.h). The logic itself is in [`main/classifier.c`](main/classifier.c). That file is plain C, unit-tested in [`../../test/test_classifier.c`](../../test/test_classifier.c).

| Condition | Result |
|---|---|
| Gas Rs/R0 ≤ 0.574 (10 % LEL) for 3 samples | WARNING (yellow, steady); clears above 0.620 |
| Gas Rs/R0 ≤ 0.414 (20 % LEL) for 3 samples | DANGER, siren, ALERT |
| Flame for 2 samples | DANGER |
| T ≥ 50 °C, or rise ≥ 8 °C/min | DANGER |
| Gas ADC ≥ 98 % of full scale | DANGER (fail toward alarm) |
| Gas input < 0.05 V for 3 samples (open wire) | FAULT within 0.3 s |
| No change for 60 s (stuck) | FAULT |
| DHT22 timeout / checksum / out of range | FAULT (temperature) |
| Battery < 3.4 V (cell fitted) | WARNING (low power) |
| DANGER | Latched until hazard clear for 10 s **and** operator ACK |

Hazard takes priority over fault (FR-06). A node that cannot vouch for its readings reports FAULT, never NORMAL. In FAULT the yellow LED blinks; in WARNING it is steady.

The Rs/R0 thresholds come from the log-log fit of the MQ-2 LPG curve: log₁₀(Rs/R0) = −0.47 log₁₀(ppm) + 1.31. **Commission each sensor:** after a 24–48 h burn-in, run `cal` in clean air to store R0 in NVS.

## Build and flash

```bash
. $IDF_PATH/export.sh            # ESP-IDF v5.2 or later
cd firmware/node/v1.0
idf.py set-target esp32
idf.py menuconfig                # "SAFE-NODE node": node ID 1-20, flame polarity, SF, ESP-NOW, debug console
idf.py build flash monitor
```

Every node needs a unique **node ID**, which also selects its heartbeat slot. The boot banner prints the firmware version (`git describe`) and the reset cause, so each test log can record the exact commit.

## Boot self-test (FR-10, closes AR-2)

Each `[CHECK]` line reports what was actually tested:

```
[CHECK] gas input    OK   1043 mV at AO, Rs/R0 2.41
[CHECK] flame input  OK   ADC read 3920 counts
[CHECK] DHT22        OK   27.8 C, 61.0 %RH
[CHECK] LED red      OK   pin drives high and low
...
[CHECK] LoRa radio   OK   SX1276 version 0x12
[CHECK] battery      SKIP no cell fitted (mains only)
```

A failed gas, flame, output or radio check keeps the node in FAULT until reboot.

## Console (115200 baud)

| Command | Use |
|---|---|
| `help`, `status`, `log`, `ver` | Information; `log` dumps the NVS event ring (reset causes survive reboot, FR-15) |
| `ack` | Local operator acknowledgement of a latched DANGER |
| `cal` | Capture R0 in clean air |
| `hang sensor` / `hang alarm` | TC-4: stop a safety task. The watchdog resets the node within about 3.5 s; the first frame after reboot is a FAULT carrying the reset cause |
| `flood <s>` | TC-6: logger uses 100 % of idle time on core 0 |
| `mute` / `unmute` | TC-3: silence the radio; the gateway flags the node OFFLINE |
| `fault gas\|flame\|temp` | SIM-6 / TC-5: toggle an injected sensor fault |
| `gas <ratio>` / `gas off` | Dry-run TC-1 without butane, e.g. `gas 0.40` |

Failure-injection commands exist only when *Enable the debug UART console* is on in menuconfig. Turn it off for deployment.

**Latency probe:** GPIO25 goes high when the sensor task confirms DANGER. Put a logic analyser on GPIO25 and GPIO13 (buzzer) and analyse the capture with `test/scripts/edge_latency.py`.

## Files

| Path | Contents |
|---|---|
| `main/app_config.h` | Thresholds, timings, calibration and radio settings |
| `main/classifier.c/.h` | Decision logic (pure C) |
| `main/main.c` | Boot, reset cause, sensor / alarm / health / diag tasks |
| `main/radio.c` | Radio and heartbeat tasks, ESP-NOW fallback |
| `main/sensors.c`, `dht22.c`, `outputs.c` | Drivers |
| `main/selftest.c` | Power-on self-test |
| `main/eventlog.c`, `console.c` | NVS event ring, logger task, console |
| `../../components/safenode_bsp` | Pin map of the carrier PCB (board-support header) |
| `../../components/safenode_proto` | 20-byte frame, CRC-16, shared with the gateway |
| `../../components/sx1276` | Register-level SX1276 driver, locked to 865–867 MHz |
