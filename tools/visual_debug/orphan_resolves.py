#!/usr/bin/env python3
"""D-010 helper: find frames whose post-process resolves have no draw in front of them
("orphan resolves") in [rtlog] lines, and check whether the game-side draw chains
(draw_chains.txt, counted in the D3D draw hook) dropped at the same swaps.

Read-only.  Writes only --out.
"""
import argparse, re, sys

RT = re.compile(r'\[rtlog\] frame (\d+)(.*)')
TOK = re.compile(r'(C[0-9A-F]+\[[^\]]*\]\{[^}]*\}|D[0-9A-F]{8}/[0-9A-F]{8}/[0-9A-F]{8}:Z[0-9A-F]{8}/[0-9A-F]{8}/[0-9A-F]{8}\*\d+|R[0-9A-F]{8}>[0-9A-F]{8}\[[^\]]*\]|S[0-9A-F]{8}/age\d+/[0-9A-F]+)')


def parse_rt(paths):
    frames = {}
    for p in paths:
        with open(p, encoding='utf-8', errors='replace') as f:
            for line in f:
                m = RT.search(line)
                if not m:
                    continue
                toks = TOK.findall(m.group(2))
                orphan, n2d0, total, last = 0, 0, 0, ''
                orphan_dst = []
                for t in toks:
                    if t[0] == 'D':
                        total += int(t.rsplit('*', 1)[1])
                        if t.startswith('D6CB07E60/'):
                            n2d0 += 1
                    if t[0] == 'R' and last and last[0] == 'R' and not t.startswith('R301D6') and not t.startswith('R301D7'):
                        src = t[1:9]
                        if last[1:9] == src or src in ('3014B000', '30103000'):
                            orphan += 1
                            orphan_dst.append(t[10:18])
                    last = t
                frames[int(m.group(1))] = (orphan, n2d0, total, ' '.join(orphan_dst))
    return frames


def parse_chains(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        head = f.readline()
        first = int(re.search(r'swaps (\d+)', head).group(1))
        rows = []
        for line in f:
            if '|' in line:
                a, b = line.split('|', 1)
                rows.append(([int(x) for x in a.split()], b.strip()))
    return first, rows


def corr(x, y):
    n = len(x)
    if n < 3:
        return 0.0
    mx, my = sum(x) / n, sum(y) / n
    sxy = sum((a - mx) * (b - my) for a, b in zip(x, y))
    sx = sum((a - mx) ** 2 for a in x) ** 0.5
    sy = sum((b - my) ** 2 for b in y) ** 0.5
    return sxy / (sx * sy) if sx and sy else 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('chains')
    ap.add_argument('logs', nargs='+')
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    fr = parse_rt(a.logs)
    first, rows = parse_chains(a.chains)
    n = max(len(r[0]) for r in rows)
    ctot = [sum(r[0][s] for r in rows if s < len(r[0])) for s in range(n)]
    out = ['rtlog frames %d (%s..%s); chain swaps %d..%d' % (len(fr), min(fr) if fr else '-', max(fr) if fr else '-', first, first + n - 1)]
    # align renderer frame numbers with swap numbers by maximising correlation of draw totals
    best = (-2, 0)
    for off in range(-8, 9):
        xs, ys = [], []
        for s in range(n):
            f = first + s + off
            if f in fr:
                xs.append(fr[f][2]); ys.append(ctot[s])
        c = corr(xs, ys)
        if c > best[0]:
            best = (c, off)
    off = best[1]
    out.append('alignment: renderer frame = swap %+d (corr %.3f)' % (off, best[0]))
    allf = sorted(fr)
    norph = sum(1 for f in allf if fr[f][0] > 0)
    out.append('frames with orphan resolves: %d of %d' % (norph, len(allf)))
    hist = {}
    for f in allf:
        k = (fr[f][0], fr[f][1])
        hist.setdefault(k, []).append(f)
    out.append('histogram over ALL logged frames (orphans, n2D0): count [first..last frame]')
    for k in sorted(hist, key=lambda k: -len(hist[k])):
        v = hist[k]
        out.append('  %s: %d [%d..%d]' % (k, len(v), v[0], v[-1]))
    out.append('')
    out.append('frame orphans n2D0 rt_draws chain_draws  orphan_dsts')
    orph_series = []
    for s in range(n):
        f = first + s + off
        if f not in fr:
            orph_series.append(0)
            continue
        o, d, t, dst = fr[f]
        orph_series.append(o)
        out.append('%5d %7d %4d %8d %11d  %s' % (f, o, d, t, ctot[s], dst))
    out.append('')
    out.append('chains most correlated (negatively) with orphan count in the chain window:')
    cs = []
    for i, (c, desc) in enumerate(rows):
        series = [c[s] if s < len(c) else 0 for s in range(n)]
        cs.append((corr(series, orph_series), i, desc, sum(1 for v in series if v)))
    for c, i, desc, nz in sorted(cs)[:12]:
        out.append('  %6.3f chain %d (%d swaps active): %s' % (c, i, nz, desc))
    with open(a.out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(out) + '\n')
    print('\n'.join(out[:4]))


if __name__ == '__main__':
    sys.exit(main())
