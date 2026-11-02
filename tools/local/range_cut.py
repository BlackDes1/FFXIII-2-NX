#!/usr/bin/env python3
"""Cuts byte ranges of the extracted game files into small chunk files (default 18 MB) for transfer, with a SHA-256
manifest. Read-only on the game files; chunk names are reused (r000.bin ...) so nothing needs deleting.
usage: range_cut.py GAME_DIR OUT_DIR RANGES_FILE [--max MB]   (RANGES_FILE lines: RANGE <path> <offset> <length>)"""
import hashlib, os, sys
game, out, rf = sys.argv[1:4]
mx = int(sys.argv[sys.argv.index("--max") + 1]) << 20 if "--max" in sys.argv else 18 << 20
os.makedirs(out, exist_ok=True)
chunks = []
for line in open(rf):
    t = line.split()
    if len(t) != 4 or t[0] != "RANGE": continue
    p, off, ln = t[1].lstrip("\\/").replace("\\", "/"), int(t[2]), int(t[3])
    while ln > 0:
        n = min(ln, mx); chunks.append((p, off, n)); off += n; ln -= n
with open(os.path.join(out, "manifest.txt"), "w") as m:
    for i, (p, off, n) in enumerate(chunks):
        with open(os.path.join(game, p), "rb") as f:
            f.seek(off); b = f.read(n)
        name = f"r{i:03d}.bin"
        with open(os.path.join(out, name), "wb") as w: w.write(b)
        m.write(f"{name} {p} {off} {len(b)} {hashlib.sha256(b).hexdigest()}\n")
print(f"{len(chunks)} chunks, {sum(c[2] for c in chunks) >> 20} MB")
