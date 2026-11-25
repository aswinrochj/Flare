"""SQLite history: node table, event log, SMS outbox and dashboard commands."""

from __future__ import annotations

import json
import sqlite3
import threading
import time
from typing import Any

SCHEMA = """
CREATE TABLE IF NOT EXISTS nodes (
    id          INTEGER PRIMARY KEY,
    zone        TEXT,
    name        TEXT,
    state       TEXT,
    flags       TEXT DEFAULT '[]',
    gas         REAL,
    temp_c      REAL,
    vbat_mv     INTEGER,
    rssi        INTEGER,
    snr         REAL,
    seq         INTEGER,
    via         TEXT,
    reset_cause INTEGER,
    reset_count INTEGER,
    uptime_s    INTEGER,
    offline     INTEGER DEFAULT 0,
    last_seen   REAL
);
CREATE TABLE IF NOT EXISTS events (
    id      INTEGER PRIMARY KEY AUTOINCREMENT,
    ts      REAL NOT NULL,
    node    INTEGER,
    type    TEXT NOT NULL,
    state   TEXT,
    detail  TEXT
);
CREATE INDEX IF NOT EXISTS events_ts ON events(ts);
CREATE TABLE IF NOT EXISTS sms_outbox (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    created    REAL NOT NULL,
    to_number  TEXT NOT NULL,
    body       TEXT NOT NULL,
    attempts   INTEGER DEFAULT 0,
    next_try   REAL NOT NULL,
    last_error TEXT,
    sent_at    REAL
);
CREATE TABLE IF NOT EXISTS commands (
    id      INTEGER PRIMARY KEY AUTOINCREMENT,
    created REAL NOT NULL,
    line    TEXT NOT NULL,
    sent_at REAL
);
CREATE TABLE IF NOT EXISTS kv (
    key   TEXT PRIMARY KEY,
    value TEXT
);
"""

NODE_FIELDS = (
    "zone", "name", "state", "flags", "gas", "temp_c", "vbat_mv", "rssi", "snr", "seq", "via",
    "reset_cause", "reset_count", "uptime_s", "offline", "last_seen",
)


class Store:
    """Thread-safe wrapper around one SQLite connection."""

    def __init__(self, path: str = "safenode.db"):
        self._db = sqlite3.connect(path, check_same_thread=False)
        self._db.row_factory = sqlite3.Row
        self._lock = threading.Lock()
        with self._lock:
            self._db.executescript(SCHEMA)
            self._db.commit()

    def close(self) -> None:
        with self._lock:
            self._db.close()

    # ---- nodes ---------------------------------------------------------
    def upsert_node(self, node_id: int, **fields: Any) -> None:
        fields = {k: v for k, v in fields.items() if k in NODE_FIELDS}
        if "flags" in fields and not isinstance(fields["flags"], str):
            fields["flags"] = json.dumps(fields["flags"])
        with self._lock:
            self._db.execute("INSERT OR IGNORE INTO nodes (id) VALUES (?)", (node_id,))
            if fields:
                sets = ", ".join(f"{k} = ?" for k in fields)
                self._db.execute(f"UPDATE nodes SET {sets} WHERE id = ?", (*fields.values(), node_id))
            self._db.commit()

    def node(self, node_id: int) -> dict | None:
        with self._lock:
            row = self._db.execute("SELECT * FROM nodes WHERE id = ?", (node_id,)).fetchone()
        return _node_dict(row) if row else None

    def nodes(self) -> list[dict]:
        with self._lock:
            rows = self._db.execute("SELECT * FROM nodes ORDER BY id").fetchall()
        return [_node_dict(r) for r in rows]

    # ---- events --------------------------------------------------------
    def add_event(self, type_: str, node: int | None = None, state: str | None = None,
                  detail: dict | None = None, ts: float | None = None) -> None:
        with self._lock:
            self._db.execute(
                "INSERT INTO events (ts, node, type, state, detail) VALUES (?, ?, ?, ?, ?)",
                (ts or time.time(), node, type_, state, json.dumps(detail or {})),
            )
            self._db.commit()

    def events(self, limit: int = 50, node: int | None = None) -> list[dict]:
        q = "SELECT * FROM events"
        args: tuple = ()
        if node is not None:
            q += " WHERE node = ?"
            args = (node,)
        q += " ORDER BY id DESC LIMIT ?"
        with self._lock:
            rows = self._db.execute(q, (*args, limit)).fetchall()
        return [dict(r, detail=json.loads(r["detail"] or "{}")) for r in rows]

    # ---- SMS outbox ------------------------------------------------------
    def sms_add(self, to_number: str, body: str, now: float) -> None:
        with self._lock:
            self._db.execute(
                "INSERT INTO sms_outbox (created, to_number, body, next_try) VALUES (?, ?, ?, ?)",
                (now, to_number, body, now),
            )
            self._db.commit()

    def sms_due(self, now: float) -> list[dict]:
        with self._lock:
            rows = self._db.execute(
                "SELECT * FROM sms_outbox WHERE sent_at IS NULL AND next_try <= ? ORDER BY id", (now,)
            ).fetchall()
        return [dict(r) for r in rows]

    def sms_mark_sent(self, sms_id: int, now: float) -> None:
        with self._lock:
            self._db.execute("UPDATE sms_outbox SET sent_at = ?, attempts = attempts + 1 WHERE id = ?", (now, sms_id))
            self._db.commit()

    def sms_mark_failed(self, sms_id: int, error: str, next_try: float) -> None:
        with self._lock:
            self._db.execute(
                "UPDATE sms_outbox SET attempts = attempts + 1, last_error = ?, next_try = ? WHERE id = ?",
                (error[:200], next_try, sms_id),
            )
            self._db.commit()

    def sms_recent(self, limit: int = 10) -> list[dict]:
        with self._lock:
            rows = self._db.execute("SELECT * FROM sms_outbox ORDER BY id DESC LIMIT ?", (limit,)).fetchall()
        return [dict(r) for r in rows]

    # ---- commands to the gateway ----------------------------------------
    def add_command(self, line: str, now: float | None = None) -> None:
        with self._lock:
            self._db.execute("INSERT INTO commands (created, line) VALUES (?, ?)", (now or time.time(), line))
            self._db.commit()

    def pending_commands(self) -> list[dict]:
        with self._lock:
            rows = self._db.execute("SELECT * FROM commands WHERE sent_at IS NULL ORDER BY id").fetchall()
        return [dict(r) for r in rows]

    def mark_command_sent(self, cmd_id: int, now: float) -> None:
        with self._lock:
            self._db.execute("UPDATE commands SET sent_at = ? WHERE id = ?", (now, cmd_id))
            self._db.commit()

    # ---- key/value (gateway status) --------------------------------------
    def set_kv(self, key: str, value: Any) -> None:
        with self._lock:
            self._db.execute("INSERT OR REPLACE INTO kv (key, value) VALUES (?, ?)", (key, json.dumps(value)))
            self._db.commit()

    def get_kv(self, key: str, default: Any = None) -> Any:
        with self._lock:
            row = self._db.execute("SELECT value FROM kv WHERE key = ?", (key,)).fetchone()
        return json.loads(row["value"]) if row else default


def _node_dict(row: sqlite3.Row) -> dict:
    d = dict(row)
    try:
        d["flags"] = json.loads(d.get("flags") or "[]")
    except json.JSONDecodeError:
        d["flags"] = []
    d["offline"] = bool(d.get("offline"))
    return d
