#!/usr/bin/env python3
"""Reads a startup read trace (SDK cvar sa2_io_trace_record, format RXSTRC01 in sdk/src/filesystem/devices/startup_trace.cpp)
and prints the files and merged byte ranges the game read.
usage: io_trace.py TRACE [--ranges] [--align N]"""
import struct, sys
from collections import defaultdict
d = open(sys.argv[1], "rb").read()
assert d[:8] == b"RXSTRC01", "not a RXSTRC01 trace"
ver, nf, ne, flags = struct.unpack_from("<IIII", d, 8)
o = 24; files = []
for _ in range(nf):
    n = struct.unpack_from("<H", d, o)[0]; o += 2
    p = d[o:o + n].decode("utf-8", "replace"); o += n
    size, wt = struct.unpack_from("<QQ", d, o); o += 16
    files.append((p, size))
reads = defaultdict(list); opens = defaultdict(int); last_t = 0
for i in range(ne):
    kind, _, thr, fi, off, ln, t = struct.unpack_from("<BBHIQII", d, o + 24 * i)
    last_t = max(last_t, t)
    if kind == 0: reads[fi].append((off, ln))
    else: opens[fi] += 1
align = int(sys.argv[sys.argv.index("--align") + 1]) if "--align" in sys.argv else 1
print(f"{nf} files, {ne} events, last at {last_t / 1e6:.1f} s")
for fi, (p, size) in enumerate(files):
    rs = sorted((off // align * align, -(-(off + ln) // align) * align) for off, ln in reads[fi])
    merged = []
    for a, b in rs:
        if merged and a <= merged[-1][1]: merged[-1][1] = max(merged[-1][1], b)
        else: merged.append([a, b])
    tot = sum(b - a for a, b in merged)
    print(f"{p}  size {size}  opens {opens[fi]}  reads {len(reads[fi])}  bytes {tot}")
    if "--ranges" in sys.argv:
        for a, b in merged: print(f"  RANGE {p} {a} {min(b, size) - a}")
