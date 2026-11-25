#!/usr/bin/env python3
"""Latency statistics from a logic-analyser export (TC-1, TC-6; DPR Section VIII-A, VIII-D).

Wire channel A to the node's latency probe (GPIO25, goes high when the sensor
task confirms DANGER) or to the stimulus, and channel B to the buzzer drive
(GPIO13) or the gateway siren. Export the capture as CSV from PulseView /
Saleae Logic ("Time [s], Channel 0, Channel 1, ..."), then:

    python3 test/scripts/edge_latency.py capture.csv --start 0 --end 1 --limit-ms 500

For each rising edge on the start channel, the delay to the next rising edge
on the end channel is measured. Results are reported as mean, 95th percentile
and maximum; the maximum is compared with the objective (DPR Section VIII-D).
"""

from __future__ import annotations

import argparse
import csv
import statistics
import sys


def rising_edges(rows, col):
    edges, prev = [], None
    for t, vals in rows:
        v = vals[col]
        if prev == 0 and v == 1:
            edges.append(t)
        prev = v
    return edges


def load(path):
    rows = []
    with open(path, newline="", encoding="utf-8") as fh:
        rd = csv.reader(fh)
        next(rd, None)
        for r in rd:
            if not r:
                continue
            rows.append((float(r[0]), [int(float(x)) for x in r[1:]]))
    return rows


def delays(start, end):
    out, j = [], 0
    for t0 in start:
        while j < len(end) and end[j] < t0:
            j += 1
        if j < len(end):
            out.append(end[j] - t0)
    return out


def percentile(values, p):
    s = sorted(values)
    k = max(0, min(len(s) - 1, round(p / 100 * (len(s) - 1))))
    return s[k]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("csv")
    ap.add_argument("--start", type=int, default=0, help="start channel index (0-based, after the time column)")
    ap.add_argument("--end", type=int, default=1, help="end channel index")
    ap.add_argument("--limit-ms", type=float, default=500.0, help="objective for the worst case")
    args = ap.parse_args()
    rows = load(args.csv)
    d = [x * 1000 for x in delays(rising_edges(rows, args.start), rising_edges(rows, args.end))]
    if not d:
        print("no edge pairs found")
        return 1
    worst = max(d)
    print(f"runs {len(d)}  mean {statistics.mean(d):.2f} ms  p95 {percentile(d, 95):.2f} ms  max {worst:.2f} ms")
    print("PASS" if worst <= args.limit_ms else f"FAIL (objective {args.limit_ms} ms)")
    return 0 if worst <= args.limit_ms else 1


if __name__ == "__main__":
    sys.exit(main())
