from pathlib import Path

import pytest

from safenode_host.protocol import (Flag, Frame, FrameError, MsgType, State, crc16_ccitt_false,
                                    frame_to_line, parse_line)

VECTORS = Path(__file__).resolve().parents[2] / "test" / "vectors" / "frames.txt"


def test_crc_check_value():
    assert crc16_ccitt_false(b"123456789") == 0x29B1


def test_shared_vectors_roundtrip():
    """Same file the C test (firmware/test/test_proto.c) checks."""
    rows = [ln.split() for ln in VECTORS.read_text().splitlines() if ln and not ln.startswith("#")]
    assert len(rows) >= 5
    for r in rows:
        f = Frame(*(int(x) for x in r[:11]))
        assert f.encode().hex() == r[11]
        assert Frame.decode(bytes.fromhex(r[11])) == f


def test_decode_rejects_bad_crc_and_length():
    raw = bytearray(Frame(1, MsgType.ALERT, 5, State.DANGER).encode())
    with pytest.raises(FrameError):
        Frame.decode(bytes(raw[:19]))
    raw[7] ^= 0xFF
    with pytest.raises(FrameError, match="CRC"):
        Frame.decode(bytes(raw))


def test_parse_line_matches_documented_example():
    ev = parse_line('{"n":3,"t":"ALERT","seq":1287,"st":"DANGER","gas":0.412,"tc":31.25,"fl":1,"bat":3980,"rssi":-71,"snr":9.5}')
    assert ev.node == 3 and ev.t == "ALERT" and ev.state == "DANGER"
    assert ev.flame is True and ev.gas == 0.412 and ev.rssi == -71


def test_parse_line_ignores_boot_noise():
    assert parse_line("ets Jun  8 2016 00:22:57") is None
    assert parse_line("I (312) gateway: ready") is None
    assert parse_line("{not json") is None


def test_frame_to_line_roundtrip():
    f = Frame(2, MsgType.WARNING, 9, State.WARNING, Flag.LOW_BATT, 560, 3010, 3390)
    ev = parse_line(frame_to_line(f, rssi=-80, snr=4.0))
    assert ev.state == "WARNING" and "low_batt" in ev.flags and ev.vbat_mv == 3390
