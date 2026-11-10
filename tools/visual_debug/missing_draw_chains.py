#!/usr/bin/env python3
"""D-010 helper: for renderer frames whose post composite draws (colour 6CB07E60) are missing
in [rtlog], show which game-side draw chains (draw_chains.txt) changed count at that swap.

Usage: missing_draw_chains.py draw_chains.txt orphans.txt --offset 1 --out file
(offset: renderer frame = swap + offset, from orphan_resolves.py).  Read-only.
"""
import argparse, re, sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('chains')
    ap.add_argument('orphans')
    ap.add_argument('--offset', type=int, default=1)
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    with open(a.chains, encoding='utf-8', errors='replace') as f:
        first = int(re.search(r'swaps (\d+)', f.readline()).group(1))
        rows = [([int(x) for x in l.split('|')[0].split()], l.split('|', 1)[1].strip()) for l in f if '|' in l]
    bad = []
    with open(a.orphans, encoding='utf-8', errors='replace') as f:
        for line in f:
            p = line.split()
            if len(p) >= 5 and p[0].isdigit() and p[1].isdigit() and p[2].isdigit():
                fr, orph, n2 = int(p[0]), int(p[1]), int(p[2])
                if n2 <= 3:
                    bad.append(fr)
    out = ['frames with missing composite draws in the chain window: %s' % bad]
    for fr in bad:
        s = fr - a.offset - first
        out.append('')
        out.append('frame %d = swap %d (index %d): chains whose count differs from both neighbours' % (fr, fr - a.offset, s))
        for i, (c, desc) in enumerate(rows):
            if 1 <= s < len(c) - 1:
                v, pv, nv = c[s], c[s - 1], c[s + 1]
                if v != pv and v != nv:
                    out.append('  chain %3d: prev %d  this %d  next %d  | %s' % (i, pv, v, nv, desc[:150]))
    with open(a.out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(out) + '\n')
    print(out[0])


if __name__ == '__main__':
    sys.exit(main())
