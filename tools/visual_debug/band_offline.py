#!/usr/bin/env python3
"""ffxiii2-nx (2026-11-06, D-003 on the PC): ranks the band trap's dumped frames (capturas/band/all/*.pgm, grey).

  python3 band_offline.py <folder> [--out montage.jpg] [--top 12] [--split 336]

Per frame: the edge share at rows split-4..split+4 (the band_scan.py test, both directions counted separately) and a
transient score, the share minus the larger of its neighbours' (a band lasts a frame or a few; a scene edge such as a
horizon stays). Also a block score: the share of 32x16 blocks above the split whose change from the previous frame is
more than 3x the median block change while the blocks below the split change normally (stale blocks). Writes the top
frames by transient score (with the frame before each) into a montage and prints the table.
"""
import argparse
import glob
import os
import re

import numpy as np
from PIL import Image


def load(path):
    return np.asarray(Image.open(path), dtype=np.float32)


def edge_shares(g, split):
    cols = g[:, 40:1241:8]
    best_down, best_up = 0.0, 0.0
    for r in range(split - 4, split + 5):
        d = cols[r - 2] - cols[r + 2]
        best_down = max(best_down, float((d > 10).mean()))
        best_up = max(best_up, float((d < -10).mean()))
    return best_down, best_up


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('folder')
    ap.add_argument('--out', default=None)
    ap.add_argument('--top', type=int, default=12)
    ap.add_argument('--split', type=int, default=336)
    a = ap.parse_args()
    fs = sorted(glob.glob(os.path.join(a.folder, '*.pgm')), key=lambda f: int(re.findall(r'\d+', os.path.basename(f))[0]))
    rows = []
    prev = None
    for f in fs:
        g = load(f)
        down, up = edge_shares(g, a.split)
        block = 0.0
        if prev is not None and prev.shape == g.shape:
            d = np.abs(g - prev)
            h, w = d.shape
            bd = d[: (h // 16) * 16, : (w // 32) * 32].reshape(h // 16, 16, w // 32, 32).mean(axis=(1, 3))
            med = float(np.median(bd)) + 1.0
            top = bd[: a.split // 16]
            block = float((top > 3 * med).mean())
        rows.append([f, down, up, block])
        prev = g
    n = len(rows)
    for i in range(n):
        nb = [rows[j][1] for j in (i - 1, i + 1) if 0 <= j < n]
        rows[i].append(rows[i][1] - (max(nb) if nb else 0))
    ranked = sorted(range(n), key=lambda i: -rows[i][4])
    print('frames', n)
    print('frame                 down   up   block  transient')
    for i in ranked[: a.top]:
        r = rows[i]
        print('%-20s %5.2f %5.2f %6.2f %6.2f' % (os.path.basename(r[0]), r[1], r[2], r[3], r[4]))
    print('count transient >= 0.15:', sum(1 for r in rows if r[4] >= 0.15), ' down >= 0.46:', sum(1 for r in rows if r[1] >= 0.46))
    if a.out:
        pick = ranked[: a.top]
        tiles = []
        for i in pick:
            for j in (i - 1, i):
                if 0 <= j < n:
                    tiles.append(Image.open(rows[j][0]).convert('L').resize((640, 360)))
        cols = 4
        m = Image.new('L', (cols * 640, ((len(tiles) + cols - 1) // cols) * 360))
        for k, t in enumerate(tiles):
            m.paste(t, ((k % cols) * 640, (k // cols) * 360))
        m.save(a.out, quality=80)


if __name__ == '__main__':
    main()
