#!/usr/bin/env python3
"""ffxiii2-nx (2026-11-07, k242): band_offline.py's D-003 band score without numpy / PIL (the PC's Python), for the band
trap's dumps (PGM / PPM).

  python band_pgm.py <folder> [--split 336] [--top 15] [--out report.txt]

Per frame: the edge share at rows split-4..split+4 (rows r-2 vs r+2 over every 8th column 40..1240, down > 10 and up < -10
counted apart), a transient score = share - max(neighbours' shares) (a band lasts a frame or a few; a horizon stays), and
a block score: the share of 32x16 blocks above the split whose change from the previous frame is > 3x the median block
change (sampled every 4th pixel). Same thresholds as band_offline.py: transient >= 0.15 counts. Read-only.
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
    return (3 if tokens[0] == b'P6' else 1), int(tokens[1]), int(tokens[2]), data[pos:]


def px(img, x, y):
    ch, w, h, d = img
    o = (y * w + x) * ch
    return d[o] if ch == 1 else (d[o] * 3 + d[o + 1] * 6 + d[o + 2]) // 10


def edges(img, split):
    ch, w, h, d = img
    cols = range(40, min(1241, w), 8)
    down = up = 0.0
    for r in range(split - 4, split + 5):
        dn = upc = 0
        for x in cols:
            v = px(img, x, r - 2) - px(img, x, r + 2)
            if v > 10:
                dn += 1
            elif v < -10:
                upc += 1
        down = max(down, dn / len(cols))
        up = max(up, upc / len(cols))
    return down, up


def blocks(img):
    ch, w, h, d = img
    out = []
    for by in range(h // 16):
        row = []
        for bx in range(w // 32):
            row.append([px(img, bx * 32 + sx, by * 16 + sy) for sy in range(0, 16, 4) for sx in range(0, 32, 4)])
        out.append(row)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('folder')
    ap.add_argument('--split', type=int, default=336)
    ap.add_argument('--top', type=int, default=15)
    ap.add_argument('--out')
    a = ap.parse_args()
    fs = sorted([f for e in ('pgm', 'ppm') for f in glob.glob(os.path.join(a.folder, '*.' + e))], key=num)
    rows, prev = [], None
    for f in fs:
        img = read_pnm(f)
        if len(img[3]) < img[1] * img[2] * img[0]:
            continue  # a frame cut short when the run ended
        down, up = edges(img, a.split)
        b = blocks(img)
        block = 0.0
        if prev is not None and len(prev) == len(b):
            diffs = [[sum(abs(p - q) for p, q in zip(pb, cb)) / len(cb) for pb, cb in zip(pr, cr)]
                     for pr, cr in zip(prev, b)]
            flat = sorted(v for r in diffs for v in r)
            med = flat[len(flat) // 2] + 1.0
            top = [v for r in diffs[: a.split // 16] for v in r]
            block = sum(1 for v in top if v > 3 * med) / max(1, len(top))
        rows.append([num(f), down, up, block])
        prev = b
    n = len(rows)
    for i in range(n):
        nb = [rows[j][1] for j in (i - 1, i + 1) if 0 <= j < n]
        rows[i].append(rows[i][1] - (max(nb) if nb else 0))
    ranked = sorted(range(n), key=lambda i: -rows[i][4])
    lines = ['%s: %d frames, transient >= 0.15: %d, down >= 0.46: %d, block >= 0.5: %d' % (
        a.folder, n, sum(1 for r in rows if r[4] >= 0.15), sum(1 for r in rows if r[1] >= 0.46),
        sum(1 for r in rows if r[3] >= 0.5))]
    lines.append('frame   down    up  block transient')
    for i in ranked[: a.top]:
        r = rows[i]
        lines.append('%6d %5.2f %5.2f %6.2f %6.2f' % (r[0], r[1], r[2], r[3], r[4]))
    text = '\n'.join(lines)
    print(text)
    if a.out:
        with open(a.out, 'w', encoding='utf-8') as f:
            f.write(text + '\n')


if __name__ == '__main__':
    sys.exit(main())
