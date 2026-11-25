"""Gateway UART -> SQLite, MQTT, SMS. Answers the gateway's PING (FR-24)
and reports "gateway lost" when the gateway goes silent."""

from __future__ import annotations

import json
import logging
import time
from dataclasses import dataclass, field

from .protocol import GatewayEvent, parse_line
from .sms import SmsQueue, sms_text

log = logging.getLogger(__name__)

NODE_FRAME_TYPES = {"HEARTBEAT", "ALERT", "WARNING", "FAULT"}
UNSAFE_STATES = {"DANGER", "FAULT", "OFFLINE"}
GATEWAY_SILENT_S = 3.0           # gateway PINGs every second
DANGER_GAS_RATIO = 0.414          # firmware CFG_GAS_DANGER_X1000 / 1000
DANGER_TEMP_C = 50.0


@dataclass
class SiteConfig:
    site: str = "site1"
    nodes: dict[int, dict] = field(default_factory=dict)   # id -> {"zone", "name", "x", "y"}

    def zone(self, node_id: int) -> str:
        return str(self.nodes.get(node_id, {}).get("zone", f"zone{node_id}"))

    def name(self, node_id: int) -> str:
        return str(self.nodes.get(node_id, {}).get("name", f"Node {node_id}"))

    @classmethod
    def load(cls, path: str | None) -> "SiteConfig":
        if not path:
            return cls()
        with open(path, encoding="utf-8") as fh:
            raw = json.load(fh)
        return cls(site=raw.get("site", "site1"), nodes={int(k): v for k, v in raw.get("nodes", {}).items()})


def hazard_cause(ev: GatewayEvent) -> str:
    if ev.state == "OFFLINE":
        return "no heartbeat"
    if ev.state == "FAULT":
        faults = [f.replace("_", " ") for f in ev.flags if f.endswith("fault")]
        return ", ".join(faults) if faults else "self-test"
    if ev.flame or "flame" in ev.flags:
        return "flame"
    if ev.gas is not None and ev.gas <= DANGER_GAS_RATIO:
        return "gas"
    if ev.temp_c is not None and ev.temp_c >= DANGER_TEMP_C:
        return "heat"
    if ev.state == "WARNING" and "low_batt" in ev.flags:
        return "low battery"
    return "hazard"


class Bridge:
    def __init__(self, store, publisher, sms: SmsQueue | None, config: SiteConfig, write_line=None,
                 clock=time.time):
        self.store = store
        self.pub = publisher
        self.sms = sms
        self.cfg = config
        self.write_line = write_line or (lambda line: None)
        self.clock = clock
        self.last_rx: float | None = None
        self.gateway_lost = False
        for nid, meta in config.nodes.items():
            store.upsert_node(nid, zone=meta.get("zone"), name=meta.get("name"))

    # ------------------------------------------------------------------
    def topic(self, node_id: int, leaf: str) -> str:
        return f"safenode/{self.cfg.site}/{self.cfg.zone(node_id)}/{node_id}/{leaf}"

    def handle_line(self, line: str) -> GatewayEvent | None:
        ev = parse_line(line)
        now = self.clock()
        if ev is None:
            if line.strip():
                log.debug("gateway: %s", line.strip())
            return None
        self.last_rx = now
        if self.gateway_lost:
            self.gateway_lost = False
            self.store.add_event("GATEWAY_RESTORED", ts=now)
            self._gateway_status(now, "ok")

        if ev.t == "PING":
            self.write_line('{"t":"PONG"}')
        elif ev.t in NODE_FRAME_TYPES and ev.node:
            self._node_frame(ev, now)
        elif ev.t == "OFFLINE" and ev.node:
            self._offline(ev, now)
        elif ev.t == "RECOVERED" and ev.node:
            self.store.add_event("RECOVERED", ev.node, ev.state, ts=now)
            self.pub.publish(self.topic(ev.node, "event"), {"t": "RECOVERED", "ts": now}, qos=1)
        elif ev.t == "GW":
            self.store.set_kv("gateway", {**ev.raw, "ts": now})
            self._gateway_status(now, "ok", ev.raw)
        elif ev.t in ("BOOT", "HOSTLOST", "OPACKED"):
            self.store.add_event(f"GATEWAY_{ev.t}", ev.node, detail=ev.raw, ts=now)
        return ev

    def _node_frame(self, ev: GatewayEvent, now: float) -> None:
        nid = ev.node
        prev = self.store.node(nid) or {}
        prev_state = None if prev.get("offline") else prev.get("state")
        self.store.upsert_node(
            nid, zone=self.cfg.zone(nid), name=self.cfg.name(nid), state=ev.state, flags=ev.flags,
            gas=ev.gas, temp_c=ev.temp_c, vbat_mv=ev.vbat_mv, rssi=ev.rssi, snr=ev.snr, seq=ev.seq,
            via=ev.raw.get("via"), reset_cause=ev.reset_cause, reset_count=ev.reset_count,
            uptime_s=ev.uptime_s, offline=0, last_seen=now,
        )
        state_payload = {"state": ev.state, "flags": ev.flags, "seq": ev.seq, "ts": now}
        self.pub.publish(self.topic(nid, "state"), state_payload, qos=1, retain=True)
        self.pub.publish(self.topic(nid, "telemetry"),
                         {"gas": ev.gas, "temp_c": ev.temp_c, "vbat_mv": ev.vbat_mv, "rssi": ev.rssi,
                          "snr": ev.snr, "ts": now}, qos=0)

        changed = ev.state != prev_state
        if ev.t != "HEARTBEAT" or changed:
            cause = hazard_cause(ev)
            self.store.add_event(ev.t, nid, ev.state, {"cause": cause, "flags": ev.flags, "seq": ev.seq}, ts=now)
            self.pub.publish(self.topic(nid, "event"), {"t": ev.t, "state": ev.state, "cause": cause, "ts": now}, qos=1)
            if changed and ev.state in UNSAFE_STATES:
                self._sms(ev.state, cause, nid, now)

    def _offline(self, ev: GatewayEvent, now: float) -> None:
        nid = ev.node
        self.store.upsert_node(nid, state="OFFLINE", offline=1)
        self.store.add_event("OFFLINE", nid, "OFFLINE", {"cause": "no heartbeat"}, ts=now)
        self.pub.publish(self.topic(nid, "state"), {"state": "OFFLINE", "ts": now}, qos=1, retain=True)
        self.pub.publish(self.topic(nid, "event"), {"t": "OFFLINE", "ts": now}, qos=1)
        self._sms("OFFLINE", "no heartbeat", nid, now)

    def _sms(self, state: str, cause: str, nid: int, now: float) -> None:
        if not self.sms:
            return
        when = time.strftime("%H:%M:%S", time.localtime(now))
        self.sms.enqueue(sms_text(state, cause, self.cfg.zone(nid), nid, when), now)

    def _gateway_status(self, now: float, status: str, extra: dict | None = None) -> None:
        payload = {"status": status, "ts": now}
        if extra:
            payload.update({k: v for k, v in extra.items() if k != "t"})
        self.pub.publish(f"safenode/{self.cfg.site}/gateway/status", payload, qos=1, retain=True)

    # ------------------------------------------------------------------
    def operator_ack(self, node_id: int) -> None:
        """Queue an operator acknowledgement for the gateway (dashboard button)."""
        self.store.add_command(json.dumps({"t": "OPACK", "n": node_id}, separators=(",", ":")), self.clock())
        self.store.add_event("OPERATOR_ACK", node_id, ts=self.clock())

    def tick(self) -> None:
        """Periodic work: send queued commands, detect a silent gateway, pump SMS."""
        now = self.clock()
        for cmd in self.store.pending_commands():
            self.write_line(cmd["line"])
            self.store.mark_command_sent(cmd["id"], now)
        if self.last_rx is not None and not self.gateway_lost and now - self.last_rx > GATEWAY_SILENT_S:
            self.gateway_lost = True
            log.error("gateway silent for %.1f s", now - self.last_rx)
            self.store.add_event("GATEWAY_LOST", ts=now)
            self._gateway_status(now, "lost")
        if self.sms:
            self.sms.pump(now)
