"""SMS queue with store-and-forward (FR-25).

Messages are written to SQLite first and retried with back-off until the
provider accepts them, so alerts raised while the internet is down are sent
when it returns. The radio path never depends on this (FR-23).
"""

from __future__ import annotations

import logging
import os
from typing import Protocol

log = logging.getLogger(__name__)

MAX_BACKOFF_S = 300.0


class SmsBackend(Protocol):
    def send(self, to_number: str, body: str) -> None:
        """Deliver one message or raise an exception."""


class ConsoleSms:
    """Prints messages instead of sending them (demo and tests)."""

    def __init__(self) -> None:
        self.sent: list[tuple[str, str]] = []

    def send(self, to_number: str, body: str) -> None:
        self.sent.append((to_number, body))
        log.warning("[SMS] to %s: %s", to_number, body)


class TwilioSms:
    """Twilio REST API. Credentials come from the environment (.env, git-ignored)."""

    URL = "https://api.twilio.com/2010-04-01/Accounts/{sid}/Messages.json"

    def __init__(self, sid: str, token: str, from_number: str, timeout: float = 10.0):
        self.sid, self.token, self.from_number, self.timeout = sid, token, from_number, timeout

    @classmethod
    def from_env(cls) -> "TwilioSms | None":
        sid = os.environ.get("TWILIO_ACCOUNT_SID")
        token = os.environ.get("TWILIO_AUTH_TOKEN")
        from_number = os.environ.get("TWILIO_FROM")
        if sid and token and from_number:
            return cls(sid, token, from_number)
        return None

    def send(self, to_number: str, body: str) -> None:
        import requests  # imported lazily so the demo runs without it

        r = requests.post(
            self.URL.format(sid=self.sid),
            data={"From": self.from_number, "To": to_number, "Body": body},
            auth=(self.sid, self.token),
            timeout=self.timeout,
        )
        if r.status_code >= 300:
            raise RuntimeError(f"Twilio HTTP {r.status_code}: {r.text[:120]}")


class SmsQueue:
    def __init__(self, store, backend: SmsBackend, recipients: list[str]):
        self.store = store
        self.backend = backend
        self.recipients = [r for r in recipients if r]

    def enqueue(self, body: str, now: float) -> int:
        for to in self.recipients:
            self.store.sms_add(to, body, now)
        return len(self.recipients)

    def pump(self, now: float) -> int:
        """Try every due message once. Returns the number delivered."""
        delivered = 0
        for msg in self.store.sms_due(now):
            try:
                self.backend.send(msg["to_number"], msg["body"])
            except Exception as exc:  # noqa: BLE001 - any provider/network error means retry later
                backoff = min(MAX_BACKOFF_S, 10.0 * (2 ** msg["attempts"]))
                self.store.sms_mark_failed(msg["id"], str(exc), now + backoff)
                log.info("SMS %s failed (%s); retry in %.0f s", msg["id"], exc, backoff)
            else:
                self.store.sms_mark_sent(msg["id"], now)
                delivered += 1
        return delivered


def sms_text(state: str, cause: str, zone: str, node_id: int, when: str) -> str:
    """Technical Documentation V-E: 'SAFE-NODE <STATE> <cause> at <zone> (node <id>), <time>. Reply ACK <id>.'"""
    return f"SAFE-NODE {state} {cause} at {zone} (node {node_id}), {when}. Reply ACK {node_id}."
