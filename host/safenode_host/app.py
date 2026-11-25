"""Command-line entry point: bridge + dashboard in one process.

    python -m safenode_host --port /dev/ttyUSB0          # real gateway
    python -m safenode_host --demo                       # emulated gateway, no hardware
"""

from __future__ import annotations

import argparse
import logging
import os
import threading
import time

from . import __version__
from .bridge import Bridge, SiteConfig
from .mqtt_pub import MqttPublisher, NullPublisher
from .sms import ConsoleSms, SmsQueue, TwilioSms
from .store import Store

log = logging.getLogger("safenode")


class SerialLink:
    def __init__(self, port: str, baud: int = 115200):
        import serial  # pyserial

        self._s = serial.Serial(port, baud, timeout=0.1)

    def readline(self, timeout: float = 0.1) -> str | None:
        line = self._s.readline()
        if not line:
            return None
        return line.decode("utf-8", errors="replace")

    def write(self, line: str) -> None:
        self._s.write((line.strip() + "\n").encode())


def load_dotenv(path: str = ".env") -> None:
    """Minimal .env loader (KEY=VALUE lines) so no extra dependency is needed."""
    if not os.path.exists(path):
        return
    with open(path, encoding="utf-8") as fh:
        for raw in fh:
            line = raw.strip()
            if line and not line.startswith("#") and "=" in line:
                k, v = line.split("=", 1)
                os.environ.setdefault(k.strip(), v.strip().strip('"').strip("'"))


def parse_args(argv=None):
    p = argparse.ArgumentParser(prog="safenode_host", description="SAFE-NODE host: gateway bridge, MQTT, SQLite, SMS and dashboard")
    src = p.add_mutually_exclusive_group(required=True)
    src.add_argument("--port", help="gateway serial port, e.g. /dev/ttyUSB0 or COM5")
    src.add_argument("--demo", action="store_true", help="use the built-in gateway emulator (no hardware)")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--config", default=None, help="site config JSON (see config.example.json)")
    p.add_argument("--db", default="safenode.db", help="SQLite file (default: safenode.db)")
    p.add_argument("--mqtt-host", default="localhost")
    p.add_argument("--mqtt-port", type=int, default=1883)
    p.add_argument("--no-mqtt", action="store_true", help="do not publish to MQTT")
    p.add_argument("--http-host", default="127.0.0.1")
    p.add_argument("--http-port", type=int, default=5000)
    p.add_argument("--raw-log", default=None,
                   help="append every gateway line with a host timestamp (input for test/scripts/pdr.py)")
    p.add_argument("--sms-to", default=os.environ.get("SMS_TO", ""), help="comma-separated recipients")
    p.add_argument("-v", "--verbose", action="store_true")
    p.add_argument("--version", action="version", version=f"%(prog)s {__version__}")
    return p.parse_args(argv)


def main(argv=None) -> int:
    load_dotenv()
    args = parse_args(argv)
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                        format="%(asctime)s %(levelname)s %(name)s: %(message)s")

    cfg = SiteConfig.load(args.config)
    if args.demo and not cfg.nodes:
        cfg.nodes = {
            1: {"zone": "store", "name": "Store room", "x": 20, "y": 30},
            2: {"zone": "kitchen", "name": "Canteen kitchen", "x": 55, "y": 55},
            3: {"zone": "workshop", "name": "Workshop", "x": 80, "y": 25},
        }
    store = Store(args.db)
    pub = NullPublisher() if args.no_mqtt else MqttPublisher(args.mqtt_host, args.mqtt_port)
    backend = TwilioSms.from_env() or ConsoleSms()
    recipients = [r.strip() for r in args.sms_to.split(",") if r.strip()] or (["+910000000000"] if args.demo else [])
    sms = SmsQueue(store, backend, recipients)
    log.info("SMS backend: %s, %d recipient(s)", type(backend).__name__, len(sms.recipients))

    if args.demo:
        from .emulator import GatewayEmulator

        link = GatewayEmulator()
        log.info("demo mode: emulated gateway with nodes 1-3 (scenario repeats every 120 s)")
    else:
        link = SerialLink(args.port, args.baud)
        log.info("gateway on %s @ %d baud", args.port, args.baud)

    bridge = Bridge(store, pub, sms, cfg, write_line=link.write)

    stop = threading.Event()

    raw = open(args.raw_log, "a", encoding="utf-8") if args.raw_log else None

    def pump() -> None:
        last_tick = 0.0
        while not stop.is_set():
            line = link.readline(0.1)
            if line:
                if raw:
                    raw.write(f"{time.time():.3f} {line.strip()}\n")
                    raw.flush()
                bridge.handle_line(line)
            if time.monotonic() - last_tick >= 0.2:
                last_tick = time.monotonic()
                bridge.tick()

    threading.Thread(target=pump, name="bridge", daemon=True).start()

    from .dashboard import create_app

    app = create_app(store, bridge, cfg)
    log.info("dashboard: http://%s:%d/", args.http_host, args.http_port)
    try:
        app.run(host=args.http_host, port=args.http_port, debug=False, use_reloader=False)
    finally:
        stop.set()
        pub.close()
    return 0
