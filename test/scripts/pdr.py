#!/usr/bin/env python3
"""Packet delivery ratio and offline-detection times from a host raw log (TC-3, TC-9).

Record the log during a bench run:
    python -m safenode_host --port /dev/ttyUSB0 --raw-log run.log
Then:
    python3 test/scripts/pdr.py run.log [--min-pdr 99]

PDR per node = distinct sequence numbers received / sequence numbers sent
(span of the first and last seq, allowing for 16-bit wrap). Duplicates are
not counted twice. OFFLINE events are listed with the time since that node's
last frame, to compare with the 10 s objective (FR-21).
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import defaultdict


def analyse(lines):
    seqs = defaultdict(list)
    last_seen: dict[int, float] = {}
    offline = []
    for raw in lines:
        try:
            ts_s, payload = raw.split(" ", 1)
            ts = float(ts_s)
            obj = json.loads(payload)
        except (ValueError, json.JSONDecodeError):
            continue
        n = obj.get("n")
        t = obj.get("t")
        if n is None:
            continue
        if t in ("HEARTBEAT", "ALERT", "WARNING", "FAULT") and "seq" in obj:
            seqs[n].append(obj["seq"])
            last_seen[n] = ts
        elif t == "OFFLINE":
            offline.append((n, ts, ts - last_seen.get(n, ts)))
    rows = []
    for n in sorted(seqs):
        s = seqs[n]
        sent = 0
        for a, b in zip(s, s[1:]):
            sent += (b - a) % 65536 or 0
        sent += 1
        received = len(set(s))
        rows.append((n, received, sent, 100.0 * min(received, sent) / sent))
    return rows, offline


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("log")
    ap.add_argument("--min-pdr", type=float, default=99.0, help="pass threshold in %% (FR-22)")
    args = ap.parse_args()
    with open(args.log, encoding="utf-8") as fh:
        rows, offline = analyse(fh)
    ok = True
    print(f"{'node':>4} {'received':>9} {'sent':>6} {'PDR %':>7}")
    for n, rx, tx, pdr in rows:
        flag = "" if pdr >= args.min_pdr else "  FAIL"
        ok &= pdr >= args.min_pdr
        print(f"{n:>4} {rx:>9} {tx:>6} {pdr:>7.2f}{flag}")
    for n, ts, gap in offline:
        print(f"OFFLINE node {n} at {ts:.3f}: {gap:.2f} s after its last frame {'(<= 10 s)' if gap <= 10 else 'FAIL'}")
        ok &= gap <= 10.5
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
