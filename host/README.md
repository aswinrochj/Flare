# Host software

Runs on a Raspberry Pi 4 or a PC next to the gateway. One process provides:

- **Bridge**: reads the gateway's JSON lines from its serial port and answers its PING every second (FR-24). It reports "gateway lost" if the gateway is silent for more than 3 s.
- **SQLite history** (`safenode.db`): node table, event log, SMS outbox, dashboard commands.
- **MQTT** to Mosquitto. Topics follow Tech. Doc. V-E: `safenode/<site>/<zone>/<node>/state` (retained, QoS 1), `…/telemetry` (QoS 0), `…/event` (QoS 1) and `safenode/<site>/gateway/status`. If no broker is running, the rest keeps working.
- **SMS queue with store-and-forward** (FR-25). DANGER, FAULT and OFFLINE alerts are written to SQLite first and retried with back-off until the provider accepts them. Twilio is used when credentials are set; otherwise messages are printed to the console.
- **Flask dashboard**: a colour-coded floor map that refreshes every second, an event log, the SMS outbox, and an *Acknowledge* button for latched alarms.

## Setup

```bash
cd host
python3 -m venv .venv
source .venv/bin/activate          # Windows: .venv\Scripts\activate
pip install -r requirements.txt
```

Optional: install the Mosquitto broker (`sudo apt install mosquitto` on Raspberry Pi OS / Ubuntu).

## Run without hardware (demo)

```bash
python -m safenode_host --demo --no-mqtt
```

Open http://127.0.0.1:5000. A built-in gateway emulator plays this scenario every 120 s:

| Time | Event |
|---|---|
| 15–31 s | Node 2 gas rises: WARNING, then DANGER (latched). SMS queued |
| 35 s | Gas clears; node 2 stays DANGER until you press **Acknowledge** |
| 45–60 s | Node 3 gas-sensor wire open: FAULT |
| 70 s | Node 1 loses power: OFFLINE within 10 s |
| 95 s | Node 1 back: RECOVERED |

## Run with the gateway

```bash
cp config.example.json config.json       # node id -> zone, name, position on the floor map
cp .env.example .env                     # optional: Twilio credentials and SMS recipients
python bridge.py --port /dev/ttyUSB0 --config config.json --raw-log run.log
# same as: python -m safenode_host --port /dev/ttyUSB0 ...
```

| Option | Default | Meaning |
|---|---|---|
| `--port` / `--demo` | — | Gateway serial port, or the built-in emulator |
| `--config` | — | Site name and node map (`config.example.json`) |
| `--db` | `safenode.db` | SQLite file |
| `--mqtt-host`, `--mqtt-port`, `--no-mqtt` | `localhost`, `1883` | MQTT broker |
| `--http-host`, `--http-port` | `127.0.0.1`, `5000` | Dashboard. Use `--http-host 0.0.0.0` to open it from other machines on the LAN |
| `--sms-to` | `$SMS_TO` | Comma-separated recipients |
| `--raw-log` | — | Append every gateway line with a timestamp; input for `test/scripts/pdr.py` |

Keep the SMS API keys in `.env`, which is git-ignored. Never commit them.

## Tests

```bash
pip install -r requirements-dev.txt
python -m pytest -q
```

The tests cover the protocol codec (cross-checked with the C firmware through `test/vectors/frames.txt`), the bridge (PING/PONG, MQTT topics, SMS only on state changes, gateway-lost detection, operator ACK), the SMS store-and-forward path, and a full end-to-end scenario through the emulator, store and dashboard API. The scenario includes the FR-21 offline bound.
