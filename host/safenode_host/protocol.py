"""SAFE-NODE protocol: 20-byte node frame and gateway UART JSON lines.

Mirrors firmware/components/safenode_proto (Technical Documentation, Section V).
The C and Python implementations are cross-checked against the shared vectors
in test/vectors/frames.txt.
"""

from __future__ import annotations

import json
import struct
from dataclasses import dataclass, field
from enum import IntEnum

FRAME_LEN = 20
ACK_LEN = 4
SEQ_OPERATOR_ACK = 0xFFFF


class MsgType(IntEnum):
    HEARTBEAT = 0x01
    ALERT = 0x02
    WARNING = 0x03
    FAULT = 0x04
    ACK = 0x05
    BEACON = 0x06


class State(IntEnum):
    NORMAL = 0
    WARNING = 1
    DANGER = 2
    FAULT = 3


class Flag:
    FLAME = 1 << 0
    GAS_FAULT = 1 << 1
    TEMP_FAULT = 1 << 2
    FLAME_FAULT = 1 << 3
    LOW_BATT = 1 << 4
    LATCHED = 1 << 5
    VIA_ESPNOW = 1 << 6

    NAMES = {
        FLAME: "flame",
        GAS_FAULT: "gas_fault",
        TEMP_FAULT: "temp_fault",
        FLAME_FAULT: "flame_fault",
        LOW_BATT: "low_batt",
        LATCHED: "latched",
        VIA_ESPNOW: "via_espnow",
    }

    @classmethod
    def names(cls, flags: int) -> list[str]:
        return [n for bit, n in cls.NAMES.items() if flags & bit]


def crc16_ccitt_false(data: bytes) -> int:
    """CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no xorout."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


class FrameError(ValueError):
    pass


@dataclass
class Frame:
    node_id: int
    type: int
    seq: int
    state: int
    flags: int = 0
    gas_x1000: int = 0
    temp_x100: int = 0
    vbat_mv: int = 0
    reset_cause: int = 0
    reset_count: int = 0
    uptime_s: int = 0
    auth: int = 0

    def encode(self) -> bytes:
        up = min(self.uptime_s, 0xFFFFFF)
        body = struct.pack(
            "<BBHBBHhHB",
            self.node_id,
            self.type,
            self.seq,
            self.state,
            self.flags,
            self.gas_x1000,
            self.temp_x100,
            self.vbat_mv,
            (self.reset_cause & 0x0F) | ((self.reset_count & 0x0F) << 4),
        )
        body += up.to_bytes(3, "little") + struct.pack("<H", self.auth)
        return body + struct.pack("<H", crc16_ccitt_false(body))

    @classmethod
    def decode(cls, buf: bytes) -> "Frame":
        if len(buf) != FRAME_LEN:
            raise FrameError(f"length {len(buf)} != {FRAME_LEN}")
        (crc,) = struct.unpack_from("<H", buf, 18)
        if crc != crc16_ccitt_false(buf[:18]):
            raise FrameError("bad CRC")
        node_id, typ, seq, state, flags, gas, temp, vbat, rst = struct.unpack_from("<BBHBBHhHB", buf)
        uptime = int.from_bytes(buf[13:16], "little")
        (auth,) = struct.unpack_from("<H", buf, 16)
        if not 1 <= node_id <= 20 or typ not in (1, 2, 3, 4) or state > 3:
            raise FrameError("field out of range")
        return cls(node_id, typ, seq, state, flags, gas, temp, vbat, rst & 0x0F, rst >> 4, uptime, auth)


# ---------------------------------------------------------------------------
# Gateway <-> host UART: one JSON object per line at 115200 baud (Section V-D)
# ---------------------------------------------------------------------------

@dataclass
class GatewayEvent:
    """A parsed line from the gateway."""

    t: str
    node: int | None = None
    seq: int | None = None
    state: str | None = None
    gas: float | None = None
    temp_c: float | None = None
    flame: bool | None = None
    vbat_mv: int | None = None
    rssi: int | None = None
    snr: float | None = None
    flags: list[str] = field(default_factory=list)
    reset_cause: int | None = None
    reset_count: int | None = None
    uptime_s: int | None = None
    raw: dict = field(default_factory=dict)


def parse_line(line: str) -> GatewayEvent | None:
    """Parse one gateway line. Non-JSON lines (boot log, ESP_LOG) return None."""
    line = line.strip()
    if not line.startswith("{"):
        return None
    try:
        obj = json.loads(line)
    except json.JSONDecodeError:
        return None
    if not isinstance(obj, dict) or "t" not in obj:
        return None
    fl = obj.get("fl")
    return GatewayEvent(
        t=str(obj["t"]),
        node=obj.get("n"),
        seq=obj.get("seq"),
        state=obj.get("st"),
        gas=obj.get("gas"),
        temp_c=obj.get("tc"),
        flame=None if fl is None else bool(fl),
        vbat_mv=obj.get("bat"),
        rssi=obj.get("rssi"),
        snr=obj.get("snr"),
        flags=list(obj.get("flags", [])),
        reset_cause=obj.get("rc"),
        reset_count=obj.get("rn"),
        uptime_s=obj.get("up"),
        raw=obj,
    )


def frame_to_line(f: Frame, rssi: int | None = None, snr: float | None = None) -> str:
    """Render a frame the way the gateway firmware does (used by the emulator and tests)."""
    obj: dict = {
        "n": f.node_id,
        "t": MsgType(f.type).name,
        "seq": f.seq,
        "st": State(f.state).name,
        "gas": round(f.gas_x1000 / 1000, 3),
        "tc": round(f.temp_x100 / 100, 2),
        "fl": 1 if f.flags & Flag.FLAME else 0,
        "bat": f.vbat_mv,
    }
    if rssi is not None:
        obj["rssi"] = rssi
    if snr is not None:
        obj["snr"] = snr
    obj["flags"] = Flag.names(f.flags)
    obj["rc"] = f.reset_cause
    obj["rn"] = f.reset_count
    obj["up"] = f.uptime_s
    return json.dumps(obj, separators=(",", ":"))
