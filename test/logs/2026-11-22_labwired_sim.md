# Test log: LabWired simulation, 22 November 2026

| | |
|---|---|
| Log ID | TL-2026-11-22-SIM |
| Format | IEEE Std 829-2008, test log and anomaly reports |
| Test items | Firmware v0.1 (`src/main.ino`, 382 lines), node circuit |
| Environment | LabWired, ESP32 DevKitC model at 240 MHz. Project `final_slider_monitor`: https://app.labwired.com/p/963436e296210af24c3ec1ab57304f01. Base project `final`: https://app.labwired.com/p/dharshinir2028/b2b2b0950f8219b71e729534c77f84a7 |
| Observation | LabWired serial monitor |
| Run statistics | 156.4 M cycles (0.65 s of device time) in 4 min 33 s, about 420× slower than real time |

## Executions

| ID | Input / condition | Expected | Observed | Status |
|---|---|---|---|---|
| SIM-1 | Power-on reset | Boot banner, self-checks, green LED | Banner; `[BOOT] ESP32 initialized`; four `[CHECK] … OK`; `[SYSTEM] SAFE MODE ACTIVE` | Pass (see AR-2) |
| SIM-2 | Gas and flame sliders at 4095 | DANGER: alerts, red LED, buzzer | `[ALERT] GAS LEAKAGE DETECTED`, `[ALERT] FLAME DETECTED`, `[OUTPUT] RED LED ON`, `[OUTPUT] BUZZER ON`; Hazard YES, System Fault NO | Pass |
| SIM-3 | Slider inputs on GPIO32/33 | Raw ADC printed each loop | `Gas ADC: 4095 \| Flame ADC: 4095 \| Flame detected: YES` | Pass |
| SIM-4 | LabWired electrical check | Pins and supplies within ratings | Passed with 4 warnings (`final_slider_monitor`); see AR-1 for `final` | Pass |
| SIM-5 | Both sliders below 1800 | NORMAL: green LED only, buzzer off | Code path reviewed, run not completed | Open |
| SIM-6 | UART command `X` | FAULT: yellow LED, buzzer off | Code path reviewed, not exercised | Open |

## Anomaly reports

Each report is classified per IEEE Std 1044: High = could damage hardware or hide a hazard; Medium = weakens a claimed function; Low = quality issue.

| ID | Severity | Description | Resolution | State |
|---|---|---|---|---|
| AR-1 | High | In `final`, MQ-2 AO (5 V supply) goes straight to GPIO32. The divider resistors are placed but not connected | R1 = 12 kΩ / R2 = 10 kΩ on the PCB. Update LabWired and recalibrate the 1800-count threshold | Closed in PCB rev 1.0 |
| AR-2 | High | Boot checks print "OK" without testing anything (main.ino lines 76–79) | Real open-wire, railed-input, DHT22 and output tests | Open |
| AR-3 | Medium | DHT22 wired but never read; temperature fixed at 30 °C | Read every 2 s; NaN or timeout sets the fault | Open |
| AR-4 | Medium | Faults can only be raised by UART; the 0–4095 range check never fails | Plausibility rules (DPR Table V) | Open |
| AR-5 | Medium | A single sample raises a hazard, and it clears on the next sample; no WARNING level and no latch | 3-sample confirmation, hysteresis, WARNING, latched DANGER | Open |
| AR-6 | Medium | Heartbeat prints "operating normally" even during a hazard | Heartbeat carries the real state | Open |
| AR-7 | Medium | No watchdog | Task watchdog with conditional feed (v1.0) | Open |
| AR-8 | Low | Base resistor 220 Ω (11.8 mA); all three LEDs red | R6 = 1 kΩ; red, green and yellow LEDs | Closed in PCB rev 1.0 |
| AR-9 | Low | Flame detected on a rising ADC value; many modules fall instead | Confirm polarity at commissioning | Open |
| AR-10 | Low | Raw ADC printed every loop pass, flooding the log | Status once per cycle; raw prints behind a debug flag | Open |
| AR-11 | Info | Firmware check of `final` marked out of date | Re-run checks after fixing AR-1 | Open |
| AR-12 | Info | Simulator about 420× slower than real time | Measure latency on the bench (TC-1, TC-6) | Accepted |

## Summary
4 passed, 2 open, 0 failed. The hazard path from analog input to red LED and buzzer is confirmed. Fail-safe behaviour (AR-2 to AR-7) is not yet implemented in v0.1 and is the scope of firmware v1.0.
