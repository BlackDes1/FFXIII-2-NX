#!/usr/bin/env python3
"""ffxiii2-nx (2026-11-07, k240): flicker_offline.py's single-frame flicker score without numpy / PIL, for the PC's
Python (PGM / PPM P5 / P6 frames, e.g. the band trap's capturas/band/all_* dumps).

  python flicker_pgm.py <folder> [--min 0.15] [--top 20] [--out report.txt]

Per frame (grey, sampled to 320x180): blocks of 20x20 where min(|b-a|, |b-c|) - |a-c| > 12 (a, c = neighbours);
the share of such blocks. >= --min counts as a flicker frame (the same thresholds as flicker_offline.py).
Also counts dark drop-outs: frame mean < 0.5 x both neighbours' means. Read-only.
"""
import argparse, glob, os, re, sys

W, H, B = 320, 180, 20


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
    ch = 3 if magic == b'P6' else 1
    px = data[pos:]
    out = []
    for y in range(H):
        sy = y * h // H
        row = sy * w * ch
        for x in range(W):
            o = row + (x * w // W) * ch
            out.append(px[o] if ch == 1 else (px[o] * 3 + px[o + 1] * 6 + px[o + 2]) // 10)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('folder')
    ap.add_argument('--min', type=float, default=0.15)
    ap.add_argument('--top', type=int, default=20)
    ap.add_argument('--out')
    a = ap.parse_args()
    fs = sorted([f for e in ('pgm', 'ppm') for f in glob.glob(os.path.join(a.folder, '*.' + e))], key=num)
    ims, kept = [], []
    for f in fs:  # k243: a frame cut short when the run ended is left out
        try:
            ims.append(read_pnm(f))
            kept.append(f)
        except IndexError:
            pass
    fs = kept
    means = [sum(i) / len(i) for i in ims]
    res, dark = [], []
    nbx, nby = W // B, H // B
    for i in range(1, len(ims) - 1):
        x, y, z = ims[i - 1], ims[i], ims[i + 1]
        hits = 0
        for by in range(nby):
            for bx in range(nbx):
                s = 0.0
                for yy in range(by * B, by * B + B):
                    o = yy * W + bx * B
                    for k in range(o, o + B):
                        a1, b1, c1 = x[k], y[k], z[k]
                        d1 = b1 - a1 if b1 > a1 else a1 - b1
                        d2 = b1 - c1 if b1 > c1 else c1 - b1
                        d3 = a1 - c1 if a1 > c1 else c1 - a1
                        s += (d1 if d1 < d2 else d2) - d3
                if s / (B * B) > 12:
                    hits += 1
        share = hits / (nbx * nby)
        res.append((share, num(fs[i])))
        if means[i] < 0.5 * means[i - 1] and means[i] < 0.5 * means[i + 1]:
            dark.append(num(fs[i]))
    flick = [r for r in res if r[0] >= a.min]
    lines = ['%s: %d frames, flicker frames (share >= %.2f): %d, dark drop-outs: %d %s' % (
        a.folder, len(fs), a.min, len(flick), len(dark), dark[:20])]
    for s, n in sorted(res, reverse=True)[:a.top]:
        lines.append('  frame %d share %.3f' % (n, s))
    text = '\n'.join(lines)
    print(text)
    if a.out:
        with open(a.out, 'w', encoding='utf-8') as f:
            f.write(text + '\n')


if __name__ == '__main__':
    sys.exit(main())
