#!/usr/bin/env python3
"""Reproduce the analytical figures of DPR Table X from equations (1)-(4).

    python3 tools/lora_budget.py            # print the table
    python3 tools/lora_budget.py --check    # exit 1 if any figure differs from the DPR (CI)
    python3 tools/lora_budget.py --sf 9     # long-range setting

(1) LoRa time on air (Semtech AN1200.13), explicit header, CRC on:
      Ts = 2^SF / BW,  nsym = 8 + max(ceil((8 PL - 4 SF + 44) / (4 SF)) (CR + 4), 0)
      Tair = (npre + 4.25) Ts + nsym Ts
(2) Local alarm latency  L_local <= Nc * Tsamp + tproc
(3) Offline detection    T_off   <= k * Phb + Tslot
(4) Battery backup       t_bk     = eta * C * V / P_node
"""

from __future__ import annotations

import argparse
import math
import sys


def airtime_ms(payload: int, sf: int = 7, bw_hz: int = 125_000, cr: int = 1, preamble: int = 8,
               low_dr_opt: bool | None = None) -> float:
    ts = (2 ** sf) / bw_hz * 1000.0
    if low_dr_opt is None:
        low_dr_opt = ts > 16.0
    de = 1 if low_dr_opt else 0
    nsym = 8 + max(math.ceil((8 * payload - 4 * sf + 44) / (4 * (sf - 2 * de))) * (cr + 4), 0)
    return (preamble + 4.25) * ts + nsym * ts


def budget(sf: int = 7) -> dict[str, float]:
    t_frame = airtime_ms(20, sf)
    t_ack = airtime_ms(4, sf)
    local = 3 * 100 + 1                        # Nc = 3, Tsamp = 100 ms, tproc < 1 ms
    spi_parse = 3.0                            # SPI transfers + parsing, a few ms
    gateway = 300 + t_frame + spi_parse
    retry = gateway + 200 + 80 + t_frame       # ACK timeout + max back-off + resend
    offline = 3 * 3.0 + 0.15                   # k = 3, Phb = 3 s, slot of node 1
    eta, cap_ah, v_cell, p_node = 0.85, 2.6, 3.7, 0.99
    return {
        "airtime_20B_ms": t_frame,
        "airtime_ack_ms": t_ack,
        "local_alarm_s": local / 1000.0,
        "control_room_s": gateway / 1000.0,
        "control_room_retry_s": retry / 1000.0,
        "offline_s": offline,
        "battery_1cell_h": eta * cap_ah * v_cell / p_node,
        "battery_2cell_h": eta * 2 * cap_ah * v_cell / p_node,
        "nodes_per_gateway": 3000 / 150,
    }


# Values printed in the DPR (Table X and Section VIII-B) for SF7
DPR = {
    "airtime_20B_ms": (56.6, 0.05),
    "airtime_ack_ms": (31.0, 0.05),
    "local_alarm_s": (0.30, 0.005),
    "control_room_s": (0.36, 0.005),
    "control_room_retry_s": (0.70, 0.005),
    "offline_s": (9.15, 0.001),
    "battery_1cell_h": (8.3, 0.05),
    "battery_2cell_h": (16.5, 0.05),
    "nodes_per_gateway": (20, 0),
}
OBJECTIVE = {
    "local_alarm_s": 0.5, "control_room_s": 1.0, "control_room_retry_s": 1.0, "offline_s": 10.0,
}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--sf", type=int, default=7)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    b = budget(args.sf)
    ok = True
    print(f"{'quantity':24} {'value':>9} {'DPR':>8} {'objective':>10}")
    for k, v in b.items():
        ref, tol = DPR.get(k, (None, 0))
        match = args.sf != 7 or ref is None or abs(v - ref) <= tol
        meets = k not in OBJECTIVE or v <= OBJECTIVE[k]
        ok &= match and meets
        obj = f"<= {OBJECTIVE[k]}" if k in OBJECTIVE else ""
        print(f"{k:24} {v:9.3f} {'' if ref is None else ref:>8} {obj:>10} {'' if match and meets else '  <-- MISMATCH'}")
    if args.check:
        print("all figures match the DPR" if ok else "MISMATCH", file=sys.stderr)
        return 0 if ok else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
