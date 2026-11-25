from safenode_host.sms import SmsQueue, sms_text
from safenode_host.store import Store


class FlakyBackend:
    """Fails while `online` is False, like a host whose internet link is down."""

    def __init__(self):
        self.online = False
        self.sent = []

    def send(self, to, body):
        if not self.online:
            raise ConnectionError("no route to host")
        self.sent.append((to, body))


def test_store_and_forward(tmp_path):
    store = Store(str(tmp_path / "s.db"))
    backend = FlakyBackend()
    q = SmsQueue(store, backend, ["+91111", "+91222"])
    assert q.enqueue("SAFE-NODE DANGER gas at lab (node 1), 10:00:00. Reply ACK 1.", now=0) == 2

    assert q.pump(now=0) == 0                      # internet down: kept, back-off 10 s
    assert q.pump(now=5) == 0                      # not due yet
    assert all(m["attempts"] == 1 for m in store.sms_recent())

    backend.online = True
    assert q.pump(now=10) == 2                     # delivered when the link returns
    assert q.pump(now=100) == 0                    # nothing left
    assert {to for to, _ in backend.sent} == {"+91111", "+91222"}


def test_sms_text_format():
    assert sms_text("DANGER", "flame", "store", 3, "14:05:09") == \
        "SAFE-NODE DANGER flame at store (node 3), 14:05:09. Reply ACK 3."
