# Firmware

| Path | Contents |
|---|---|
| [`node/v0.1/`](node/v0.1/) | Arduino prototype that ran in LabWired (simulation only, not fail-safe) |
| [`node/v1.0/`](node/v1.0/) | Target node firmware: ESP-IDF v5 + FreeRTOS, watchdog, plausibility checks, LoRa |
| [`gateway/`](gateway/) | Control-room gateway: node table, liveness, siren, host link |
| [`components/safenode_proto`](components/safenode_proto/) | 20-byte frame, ACK, beacon, CRC-16/CCITT-FALSE (shared) |
| [`components/sx1276`](components/sx1276/) | SX1276 / RFM95W LoRa driver, locked to 865–867 MHz |
| [`components/safenode_bsp`](components/safenode_bsp/) | Pin map of the carrier PCB rev 1.0 |
| [`test/`](test/) | Unit tests for the hardware-independent modules, run on a PC |

## Unit tests (no ESP32 needed)

```bash
make -C firmware/test
```

These tests cover:
- **`test_proto`**: CRC check value, encode/decode, rejection of bad length, CRC and fields. It also checks that the C encoder produces exactly the bytes in `test/vectors/frames.txt`, which come from the Python codec in `host/`.
- **`test_classifier`**: one case per decision rule (FR-03, FR-04, FR-06, FR-11, FR-12, FR-13, FR-16, FR-10), including transient filtering, hysteresis and the DANGER latch.
- **`test_node_table`**: the 9.15 s offline bound (FR-21), recovery, duplicate handling and siren silencing.

## Building the ESP-IDF projects

You need ESP-IDF v5.2 or later ([installation guide](https://docs.espressif.com/projects/esp-idf/en/v5.2.3/esp32/get-started/index.html)).

```bash
. $IDF_PATH/export.sh
cd firmware/node/v1.0 && idf.py set-target esp32 build
cd ../../gateway      && idf.py set-target esp32 build
```

GitHub Actions builds both projects with ESP-IDF v5.2.3 and the v0.1 sketch with the Arduino ESP32 core on every push (`.github/workflows/ci.yml`).
