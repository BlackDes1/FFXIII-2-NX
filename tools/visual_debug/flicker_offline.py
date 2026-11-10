#!/usr/bin/env python3
"""ffxiii2-nx (2026-11-06): single-frame flicker in a sequence of frames (the user's PC video 2026-11-06 16-38-50.mp4:
the 3D scene black, only sky, or black in part for one frame while the HUD stays).

  python3 flicker_offline.py <folder of frames (png/pgm/ppm)> [--top 20] [--out montage.jpg] [--json out.json]

Score per frame (grey, 320x180): blocks (20x20) where the frame differs from both neighbours by more than the
neighbours differ from each other (min(|b-a|, |b-c|) - |a-c| > 12); share of such blocks. A scene that changes
steadily scores 0; a frame that drops out and comes back scores high.
"""
import argparse
import glob
import json
import os
import re

import numpy as np
from PIL import Image


def num(f):
    m = re.findall(r'\d+', os.path.basename(f))
    return int(m[-1]) if m else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('folder')
    ap.add_argument('--top', type=int, default=20)
    ap.add_argument('--out')
    ap.add_argument('--json')
    ap.add_argument('--min', type=float, default=0.15)
    a = ap.parse_args()
    fs = sorted([f for e in ('png', 'pgm', 'ppm') for f in glob.glob(os.path.join(a.folder, '*.' + e))], key=num)
    ims = [np.asarray(Image.open(f).convert('L').resize((320, 180)), dtype=np.float32) for f in fs]
    res = []
    for i in range(1, len(ims) - 1):
        x, y, z = ims[i - 1], ims[i], ims[i + 1]
        t = np.minimum(np.abs(y - x), np.abs(y - z)) - np.abs(x - z)
        bl = t.reshape(9, 20, 16, 20).mean(axis=(1, 3))
        res.append({'frame': num(fs[i]), 'file': fs[i], 'share': float((bl > 12).mean()),
                    'dark': float(y.mean() - (x.mean() + z.mean()) / 2)})
    res.sort(key=lambda r: -r['share'])
    print('frames', len(ims), ' flicker (share >= %.2f):' % a.min, sum(1 for r in res if r['share'] >= a.min))
    for r in res[: a.top]:
        print('%8d share %.3f brightness %+.1f  %s' % (r['frame'], r['share'], r['dark'], os.path.basename(r['file'])))
    if a.json:
        json.dump(res, open(a.json, 'w'), indent=0)
    if a.out:
        pick = [r for r in res[: a.top] if r['share'] >= a.min][:8]
        idx = {num(f): k for k, f in enumerate(fs)}
        m = Image.new('L', (3 * 427, max(1, len(pick)) * 240))
        for row, r in enumerate(pick):
            k = idx[r['frame']]
            for col, j in enumerate((k - 1, k, k + 1)):
                m.paste(Image.open(fs[j]).convert('L').resize((427, 240)), (col * 427, row * 240))
        m.save(a.out, quality=80)


if __name__ == '__main__':
    main()
