"""Gateway emulator: produces the same UART JSON lines as firmware/gateway so
the bridge, dashboard, MQTT and SMS path can be run and tested without
hardware (`python -m safenode_host --demo`).

The node frames are built with the real codec (encode -> decode) and the
liveness rule mirrors firmware/gateway/main/node_table.c. The scenario loops
every 120 s:

  t =  15 s   node 2 gas rises: WARNING, then DANGER (latched)
  t =  35 s   gas clears; node 2 stays DANGER until the operator acknowledges
  t =  45 s   node 3 gas-sensor wire opens: FAULT
  t =  60 s   node 3 repaired: NORMAL
  t =  70 s   node 1 loses power: OFFLINE after 3 missed heartbeats
  t =  95 s   node 1 back: RECOVERED
"""

from __future__ import annotations

import queue
import threading
import time

from .protocol import Flag, Frame, MsgType, State, frame_to_line

HB_PERIOD = 3.0
SLOT = 0.15
OFFLINE_AFTER = 3 * HB_PERIOD          # + node slot, as in node_table.c
CYCLE = 120.0


class _Node:
    def __init__(self, nid: int):
        self.id = nid
        self.seq = 0
        self.state = State.NORMAL
        self.flags = 0
        self.gas = 2.40
        self.temp = 28.5
        self.vbat = 4050
        self.alive = True
        self.acked = False
        self.clear_since: float | None = None
        self.next_hb = SLOT * nid
        self.last_seen = 0.0
        self.offline = False


class GatewayEmulator:
    def __init__(self, nodes=(1, 2, 3), clock=time.monotonic):
        self.clock = clock
        self.t0 = clock()
        self.nodes = {n: _Node(n) for n in nodes}
        self.out: "queue.Queue[str]" = queue.Queue()
        self.next_ping = 1.0
        self.pings_unanswered = 0
        self.host_lost = False
        self.next_status = 10.0
        self._lock = threading.Lock()
        self.emit({"t": "BOOT", "fw": "emulator", "lora": True})

    # -- line I/O, same interface as the serial link --------------------
    def readline(self, timeout: float = 0.1) -> str | None:
        self.step()
        try:
            return self.out.get(timeout=timeout)
        except queue.Empty:
            self.step()
            return None

    def write(self, line: str) -> None:
        with self._lock:
            if '"PONG"' in line:
                self.pings_unanswered = 0
                self.host_lost = False
            elif '"OPACK"' in line:
                import json

                nid = json.loads(line).get("n")
                if nid in self.nodes:
                    self.nodes[nid].acked = True
                    self.emit({"n": nid, "t": "OPACKED"})

    def emit(self, obj) -> None:
        import json

        self.out.put(obj if isinstance(obj, str) else json.dumps(obj, separators=(",", ":")))

    # -- simulation ------------------------------------------------------
    def _send(self, n: _Node, typ: MsgType, now: float) -> None:
        n.seq = (n.seq + 1) & 0xFFFF
        if n.seq == 0xFFFF:
            n.seq = 0
        latched = Flag.LATCHED if n.state == State.DANGER else 0
        f = Frame(n.id, typ, n.seq, n.state, n.flags | latched, int(n.gas * 1000), int(n.temp * 100),
                  n.vbat, 0, 0, int(now))
        f = Frame.decode(f.encode())          # exercise the real codec
        if n.offline:
            n.offline = False
            self.emit({"n": n.id, "t": "RECOVERED", "st": State(f.state).name})
        n.last_seen = now
        line = frame_to_line(f, rssi=-60 - 7 * n.id, snr=9.5 - n.id)
        self.emit(line[:-1] + ',"via":"lora"}')

    def _scenario(self, n: _Node, t: float, now: float) -> None:
        prev = (n.state, n.flags)
        if n.id == 2:
            if t < 15:
                n.gas = 2.4
            elif t < 35:
                n.gas = max(0.35, 2.4 - (t - 15) * 0.12)   # WARNING ~30 s, DANGER ~31.5 s
            else:
                n.gas = min(2.4, 0.35 + (t - 35) * 0.3)
            if n.gas <= 0.414:
                if n.state != State.DANGER:
                    n.acked = False
                n.state = State.DANGER
                n.clear_since = None
            elif n.state == State.DANGER:
                if n.gas >= 0.62:
                    n.clear_since = n.clear_since or t
                    if n.acked and t - n.clear_since >= 10:
                        n.state = State.NORMAL
            elif n.gas <= 0.574:
                n.state = State.WARNING
            elif n.gas >= 0.62:
                n.state = State.NORMAL
        if n.id == 3:
            if 45 <= t < 60:
                n.state, n.flags = State.FAULT, Flag.GAS_FAULT
            else:
                n.state, n.flags = State.NORMAL, 0
        if n.id == 1:
            n.alive = not (70 <= t < 95)
        n.temp = 28.5 + 0.3 * ((int(t) // 7) % 3)
        if n.alive and (n.state, n.flags) != prev:
            typ = {State.DANGER: MsgType.ALERT, State.WARNING: MsgType.WARNING,
                   State.FAULT: MsgType.FAULT}.get(n.state, MsgType.HEARTBEAT)
            self._send(n, typ, now)

    def step(self) -> None:
        with self._lock:
            now = self.clock() - self.t0
            t = now % CYCLE
            for n in self.nodes.values():
                self._scenario(n, t, now)
                if n.alive and now >= n.next_hb:
                    self._send(n, MsgType.HEARTBEAT, now)
                if now >= n.next_hb:
                    n.next_hb += HB_PERIOD
                if not n.offline and now - n.last_seen > OFFLINE_AFTER + SLOT * n.id and n.last_seen > 0:
                    n.offline = True
                    self.emit({"n": n.id, "t": "OFFLINE", "st": "OFFLINE"})
            if now >= self.next_ping:
                self.next_ping += 1.0
                self.pings_unanswered += 1
                self.emit({"t": "PING"})
                if self.pings_unanswered >= 3 and not self.host_lost:
                    self.host_lost = True
                    self.emit({"t": "HOSTLOST"})
            if now >= self.next_status:
                self.next_status += 10.0
                self.emit({"t": "GW", "up": int(now), "nodes": len(self.nodes), "crc_err": 0,
                           "lora": True, "host": "lost" if self.host_lost else "ok"})
