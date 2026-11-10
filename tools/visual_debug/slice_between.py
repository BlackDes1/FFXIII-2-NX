#!/usr/bin/env python3
"""Write the lines of a text file between the first line starting with START and the next starting with END.
Usage: slice_between.py file START END out   (read-only on the input)"""
import sys

src, start, end, dst = sys.argv[1:5]
on, out = False, []
with open(src, encoding='utf-8', errors='replace') as f:
    for line in f:
        if not on and line.startswith(start):
            on = True
        if on:
            out.append(line)
            if line.startswith(end) and len(out) > 1:
                break
with open(dst, 'w', encoding='utf-8') as f:
    f.writelines(out)
print(len(out), 'lines')
