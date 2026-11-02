#!/usr/bin/env python3
"""Writes transferred chunks (range_cut.py) into the sparse game files, after checking each SHA-256.
usage: range_apply.py CHUNK_DIR GAME_DIR"""
import hashlib, os, sys
cdir, game = sys.argv[1:3]; ok = bad = 0
for line in open(os.path.join(cdir, "manifest.txt")):
    name, p, off, n, h = line.split()
    b = open(os.path.join(cdir, name), "rb").read()
    if len(b) != int(n) or hashlib.sha256(b).hexdigest() != h: print("BAD", name, p, off); bad += 1; continue
    with open(os.path.join(game, p), "r+b") as f: f.seek(int(off)); f.write(b)
    ok += 1
print(f"applied {ok}, bad {bad}")
