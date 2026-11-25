# Gateway firmware (ESP-IDF v5)

The gateway is an ESP32 with an SX1276 (865–867 MHz) in the control room. The SAFE-NODE carrier PCB can serve as the gateway board: its buzzer driver runs the control-room siren and its LEDs show a summary.

> **Status (25 Nov 2026).** The code is complete and builds with ESP-IDF v5.2.3 with no errors or warnings (image 789 KB). The node table passes its unit tests on a PC. It has not yet run on hardware.

## What it does

| Job | Requirement | Where |
|---|---|---|
| Receive frames over LoRa and ESP-NOW; a bad CRC counts as missing | Tech. Doc. V-C | `main.c` `handle_frame` |
| ACK ALERT / FAULT / WARNING at once; ACK duplicates but do not forward them | FR-22, V-C | `main.c`, `node_table.c` |
| Beacon every 3 s, so node heartbeats fall in their slots | Table XI | `main.c` `radio_task` |
| Flag a node OFFLINE after 3 missed heartbeats (9 s + slot, 9.15 s for node 1) | FR-21 | `node_table.c` |
| Siren on while any node is in DANGER, until the operator acknowledges | O2 | `supervisor_task` |
| Fault tone (200 ms every 2 s) for a FAULT or OFFLINE node, or when 3 PINGs in a row go unanswered (≤ 3 s) | FR-24 | `supervisor_task` |
| Forward every event to the host, one JSON object per line at 115200 baud | Tech. Doc. V-D | `forward()` |

LEDs: red = a node in DANGER, yellow = fault / offline / host lost, green = all healthy.

## UART protocol

Gateway → host (examples):

```json
{"n":3,"t":"ALERT","seq":1287,"st":"DANGER","gas":0.412,"tc":31.25,"fl":1,"bat":3980,"rssi":-71,"snr":9.5,"flags":["flame","latched"],"rc":0,"rn":0,"up":86400,"via":"lora"}
{"n":1,"t":"OFFLINE","st":"OFFLINE"}
{"n":1,"t":"RECOVERED","st":"NORMAL"}
{"t":"PING"}
{"t":"HOSTLOST"}
{"t":"GW","up":120,"nodes":3,"crc_err":0,"lora":true,"host":"ok"}
```

Host → gateway:

```json
{"t":"PONG"}
{"t":"OPACK","n":3}
```

`OPACK` silences the siren for that node and sends the node an operator acknowledgement: an ACK frame with `seq = 0xFFFF`. This is a project extension of Table XIII. The node then leaves DANGER once the hazard has been clear for 10 s.

## Build and flash

```bash
. $IDF_PATH/export.sh
cd firmware/gateway
idf.py set-target esp32
idf.py menuconfig        # "SAFE-NODE gateway": spreading factor and ESP-NOW channel (must match the nodes)
idf.py build flash
```

Then connect the host: `python -m safenode_host --port /dev/ttyUSB0` (see [`host/README.md`](../../host/README.md)). Close `idf.py monitor` first, because only one program can hold the serial port.
