import json

import pytest

from safenode_host.bridge import Bridge, SiteConfig
from safenode_host.mqtt_pub import RecordingPublisher
from safenode_host.sms import ConsoleSms, SmsQueue
from safenode_host.store import Store


class Clock:
    def __init__(self, t=1_000_000.0):
        self.t = t

    def __call__(self):
        return self.t


@pytest.fixture
def rig(tmp_path):
    clock = Clock()
    store = Store(str(tmp_path / "t.db"))
    pub = RecordingPublisher()
    backend = ConsoleSms()
    sms = SmsQueue(store, backend, ["+911234567890"])
    written = []
    cfg = SiteConfig(site="lab", nodes={2: {"zone": "kitchen", "name": "Kitchen"}})
    bridge = Bridge(store, pub, sms, cfg, write_line=written.append, clock=clock)
    return bridge, store, pub, backend, written, clock


def line(n, t, st, seq, **kw):
    obj = {"n": n, "t": t, "seq": seq, "st": st, "gas": kw.get("gas", 2.5), "tc": 28.0,
           "fl": kw.get("fl", 0), "bat": 4000, "rssi": -70, "snr": 9.0, "flags": kw.get("flags", [])}
    return json.dumps(obj)


def test_ping_answered_with_pong(rig):
    bridge, *_, written, _ = rig
    bridge.handle_line('{"t":"PING"}')
    assert written == ['{"t":"PONG"}']


def test_alert_updates_store_mqtt_and_sms(rig):
    bridge, store, pub, backend, _, clock = rig
    bridge.handle_line(line(2, "HEARTBEAT", "NORMAL", 1))
    bridge.handle_line(line(2, "ALERT", "DANGER", 2, gas=0.40, flags=["latched"]))
    node = store.node(2)
    assert node["state"] == "DANGER" and node["zone"] == "kitchen"
    topics = [m[0] for m in pub.messages]
    assert "safenode/lab/kitchen/2/state" in topics
    retained = [m for m in pub.messages if m[0].endswith("/state")]
    assert all(m[3] for m in retained) and all(m[2] == 1 for m in retained)
    bridge.tick()
    assert len(backend.sent) == 1
    to, body = backend.sent[0]
    assert body.startswith("SAFE-NODE DANGER gas at kitchen (node 2)") and body.endswith("Reply ACK 2.")
    assert store.events(1)[0]["type"] == "ALERT"


def test_repeated_heartbeats_do_not_spam(rig):
    bridge, store, _, backend, _, clock = rig
    for seq in range(1, 6):
        clock.t += 3
        bridge.handle_line(line(1, "HEARTBEAT", "NORMAL", seq))
    bridge.tick()
    assert backend.sent == []
    assert len(store.events(50)) == 1   # only the first (state change from unknown)


def test_offline_event_and_sms(rig):
    bridge, store, pub, backend, _, _ = rig
    bridge.handle_line(line(3, "HEARTBEAT", "NORMAL", 1))
    bridge.handle_line('{"n":3,"t":"OFFLINE","st":"OFFLINE"}')
    assert store.node(3)["offline"] is True
    bridge.tick()
    assert "OFFLINE no heartbeat" in backend.sent[0][1]
    bridge.handle_line('{"n":3,"t":"RECOVERED","st":"NORMAL"}')
    bridge.handle_line(line(3, "HEARTBEAT", "NORMAL", 2))
    assert store.node(3)["offline"] is False


def test_fault_cause_lists_faulty_sensor(rig):
    bridge, _, _, backend, _, _ = rig
    bridge.handle_line(line(1, "FAULT", "FAULT", 1, flags=["gas_fault"]))
    bridge.tick()
    assert "FAULT gas fault" in backend.sent[0][1]


def test_gateway_lost_after_silence(rig):
    bridge, store, pub, *_, clock = rig
    bridge.handle_line('{"t":"PING"}')
    clock.t += 2.0
    bridge.tick()
    assert not bridge.gateway_lost
    clock.t += 1.5
    bridge.tick()
    assert bridge.gateway_lost
    assert store.events(1)[0]["type"] == "GATEWAY_LOST"
    assert any(m[0] == "safenode/lab/gateway/status" and m[1]["status"] == "lost" for m in pub.messages)
    bridge.handle_line('{"t":"PING"}')
    assert not bridge.gateway_lost


def test_operator_ack_is_sent_to_gateway(rig):
    bridge, store, _, _, written, _ = rig
    bridge.operator_ack(2)
    bridge.tick()
    assert written == ['{"t":"OPACK","n":2}']
    bridge.tick()
    assert written == ['{"t":"OPACK","n":2}']   # sent once


def test_noise_lines_ignored(rig):
    bridge, store, *_ = rig
    assert bridge.handle_line("rst:0x1 (POWERON_RESET),boot:0x13") is None
    assert store.nodes() == [] or all(n["state"] is None for n in store.nodes())
