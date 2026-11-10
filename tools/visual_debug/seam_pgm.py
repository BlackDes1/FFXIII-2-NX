#!/usr/bin/env python3
"""ffxiii2-nx (2026-11-07, k241): the tile seam check at x 640 for band-trap dumps (PGM / PPM), pure Python.

  python seam_pgm.py <folder> [--x 640] [--out report.txt]

Per frame: the mean step across the seam |p[x-1] - p[x]| against the mean step between neighbouring columns
x-40 .. x+40 (the seam pair left out), over every row. A tiled scene drawn wrong in one tile (k207-k209, k222) gives a
seam step well above its neighbours in many frames, and the halves' means drift apart. Reports the mean ratio, the frames
with ratio > 2 and the mean |left half - right half| brightness. Read-only.
"""
import argparse, glob, os, re, sys


def num(f):
    m = re.findall(r'\d+', os.path.basename(f))
    return int(m[-1]) if m else 0


def read_pnm(path):
    with open(path, 'rb') as f:
        data = f.read()
    tokens, pos = [], 0
    while len(tokens) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b'#':
            while data[pos:pos + 1] not in (b'\n', b''):
                pos += 1
            continue
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        tokens.append(data[start:pos])
    pos += 1
    magic, w, h = tokens[0], int(tokens[1]), int(tokens[2])
    return (3 if magic == b'P6' else 1), w, h, data[pos:]


def grey(px, o, ch):
    return px[o] if ch == 1 else (px[o] * 3 + px[o + 1] * 6 + px[o + 2]) // 10


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('folder')
    ap.add_argument('--x', type=int, default=640)
    ap.add_argument('--out')
    a = ap.parse_args()
    fs = sorted([f for e in ('pgm', 'ppm') for f in glob.glob(os.path.join(a.folder, '*.' + e))], key=num)
    ratios, high, halves = [], [], []
    for f in fs:
        ch, w, h, px = read_pnm(f)
        if len(px) < w * h * ch:
            continue  # k243: a frame cut short when the run ended
        sx = a.x
        seam = other = 0
        n_other = 0
        left = right = 0
        for y in range(0, h, 2):
            row = y * w * ch
            vals = [grey(px, row + x * ch, ch) for x in range(sx - 41, sx + 41)]
            for i in range(len(vals) - 1):
                d = abs(vals[i] - vals[i + 1])
                if i == 40:
                    seam += d
                else:
                    other += d
                    n_other += 1
            left += sum(grey(px, row + x * ch, ch) for x in range(0, sx, 16))
            right += sum(grey(px, row + x * ch, ch) for x in range(sx, w, 16))
        rows = len(range(0, h, 2))
        s = seam / rows
        o = other / max(1, n_other)
        r = s / o if o > 0 else 0.0
        ratios.append(r)
        if r > 2:
            high.append(num(f))
        nl, nr = rows * len(range(0, sx, 16)), rows * len(range(sx, w, 16))
        halves.append(abs(left / nl - right / nr))
    if not fs:
        print('no frames in', a.folder)
        return 1
    text = ('%s: %d frames, seam step / neighbour step mean %.3f, frames with ratio > 2: %d %s, mean |left - right| '
            'brightness %.2f' % (a.folder, len(fs), sum(ratios) / len(ratios), len(high), high[:20],
                                 sum(halves) / len(halves)))
    print(text)
    if a.out:
        with open(a.out, 'w', encoding='utf-8') as f:
            f.write(text + '\n')


if __name__ == '__main__':
    sys.exit(main())
