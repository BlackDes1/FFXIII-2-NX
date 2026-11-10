#!/usr/bin/env python3
"""Contact sheet (PNG, grey) of chosen frames with their neighbours: rows = prev | frame | next, 320x180 each.
Pure Python (zlib). Usage: pgm_triplets.py folder out.png N [N ...]   (frame numbers as in fNNNNNN.pgm). Read-only."""
import os, struct, sys, zlib

sys.path.insert(0, os.path.dirname(__file__))
from flicker_pgm import read_pnm, W, H  # noqa: E402

folder, dst, *nums = sys.argv[1:]
rows = []
for n in nums:
    n = int(n)
    imgs = []
    for k in (n - 1, n, n + 1):
        p = os.path.join(folder, 'f%06d.pgm' % k)
        imgs.append(read_pnm(p) if os.path.exists(p) else [0] * (W * H))
    for y in range(H):
        line = bytearray()
        for im in imgs:
            line += bytes(im[y * W:(y + 1) * W])
            line += b'\xff\xff'
        rows.append(bytes(line))
    rows.append(b'\xff' * (3 * (W + 2)))
width, height = 3 * (W + 2), len(rows)
raw = b''.join(b'\x00' + r for r in rows)
def chunk(t, d):
    return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 0, 0, 0, 0)) + \
    chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b'')
open(dst, 'wb').write(png)
print(dst, width, height)
