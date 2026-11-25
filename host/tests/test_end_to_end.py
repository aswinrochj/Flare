"""Emulated gateway -> bridge -> store -> dashboard API, on a virtual clock."""

from safenode_host.bridge import Bridge, SiteConfig
from safenode_host.dashboard import create_app
from safenode_host.emulator import GatewayEmulator
from safenode_host.mqtt_pub import RecordingPublisher
from safenode_host.sms import ConsoleSms, SmsQueue
from safenode_host.store import Store


class Clock:
    def __init__(self):
        self.t = 0.0

    def __call__(self):
        return self.t


def run(seconds, emu, bridge, clock, dt=0.05):
    end = clock.t + seconds
    while clock.t < end:
        clock.t += dt
        emu.step()
        while not emu.out.empty():
            bridge.handle_line(emu.out.get())
        bridge.tick()


def test_full_scenario(tmp_path):
    clock = Clock()
    emu = GatewayEmulator(clock=clock)
    store = Store(str(tmp_path / "e2e.db"))
    backend = ConsoleSms()
    bridge = Bridge(store, RecordingPublisher(), SmsQueue(store, backend, ["+910"]),
                    SiteConfig(site="demo"), write_line=emu.write, clock=clock)

    run(10, emu, bridge, clock)
    assert {n["id"]: n["state"] for n in store.nodes()} == {1: "NORMAL", 2: "NORMAL", 3: "NORMAL"}
    assert not emu.host_lost                      # bridge answers PINGs

    run(25, emu, bridge, clock)                   # t = 35 s: gas leak at node 2
    assert store.node(2)["state"] == "DANGER"
    types = [e["type"] for e in store.events(100) if e["node"] == 2]
    assert "WARNING" in types and "ALERT" in types

    run(15, emu, bridge, clock)                   # t = 50 s: gas gone but DANGER latched, node 3 FAULT
    assert store.node(2)["state"] == "DANGER"
    assert store.node(3)["state"] == "FAULT"

    app = create_app(store, bridge, SiteConfig(site="demo"))
    client = app.test_client()
    assert client.post("/api/nodes/2/ack").get_json()["ok"]
    run(12, emu, bridge, clock)                   # operator ACK + 10 s clear -> NORMAL
    assert store.node(2)["state"] == "NORMAL"

    run(30, emu, bridge, clock)                   # t = 92 s: node 1 powered off at 70 s
    assert store.node(1)["offline"] is True
    offline_ev = [e for e in store.events(200) if e["type"] == "OFFLINE" and e["node"] == 1][0]
    assert 70 < offline_ev["ts"] <= 70 + 10.0             # FR-21: within 10 s of the node stopping

    run(8, emu, bridge, clock)                    # node 1 back at 95 s
    assert store.node(1)["offline"] is False

    sent = [b for _, b in backend.sent]
    assert any("DANGER" in b and "node 2" in b for b in sent)
    assert any("FAULT" in b and "node 3" in b for b in sent)
    assert any("OFFLINE" in b and "node 1" in b for b in sent)

    state = client.get("/api/state").get_json()
    assert len(state["nodes"]) == 3 and state["gateway"]["lost"] is False
    assert client.get("/").status_code == 200


def test_host_silence_reported_by_gateway():
    clock = Clock()
    emu = GatewayEmulator(clock=clock)
    for _ in range(50):                            # 5 s with nobody answering PING
        clock.t += 0.1
        emu.step()
    lines = []
    while not emu.out.empty():
        lines.append(emu.out.get())
    assert any('"HOSTLOST"' in ln for ln in lines)  # FR-24 path
