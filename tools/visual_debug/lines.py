#!/usr/bin/env python3
"""Copy line ranges of a text file (1-based, inclusive) to an output file, with line numbers.
Usage: lines.py file out A-B [C-D ...]   (read-only on the input)"""
import sys

src, dst, *ranges = sys.argv[1:]
with open(src, encoding='utf-8', errors='replace') as f:
    lines = f.readlines()
out = []
for r in ranges:
    a, b = (int(x) for x in r.split('-'))
    out.append('--- %s %d-%d\n' % (src, a, b))
    for i in range(max(1, a), min(len(lines), b) + 1):
        out.append('%5d  %s' % (i, lines[i - 1]))
with open(dst, 'w', encoding='utf-8') as f:
    f.writelines(out)
print(len(out), 'lines')
