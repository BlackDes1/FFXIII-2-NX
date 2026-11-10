#!/usr/bin/env python3
"""D-010 helper: per-swap draw-set timeline from draw_chains.txt + pass_counts.txt.

For each swap in the draw_chains window it prints the vblank stamp (pass_counts field 3),
the vblank delta, the set of active draw chains (as a set id), and whether the swap's set
is a RETURN to an earlier set (A..B..A with B != A), which is what a stale-shot redraw
looks like at the game level.  Read-only analysis; writes only the --out text file.
"""
import argparse, re, sys


def load_chains(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        head = f.readline()
        m = re.search(r'swaps (\d+)\.\.(\d+)', head)
        first = int(m.group(1)) if m else 0
        rows = []
        for line in f:
            if '|' not in line:
                continue
            nums, desc = line.split('|', 1)
            counts = [int(x) for x in nums.split()]
            rows.append((counts, desc.strip()))
    return first, rows


def load_pass(path):
    vb = {}
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            if ':' not in line:
                continue
            idx, rest = line.split(':', 1)
            try:
                base = int(idx)
            except ValueError:
                continue
            for i, tok in enumerate(rest.split()):
                p = tok.split('/')
                if len(p) >= 3:
                    try:
                        vb[base + i] = (int(p[0]), int(p[1]), int(p[2]))
                    except ValueError:
                        pass
    return vb


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('chains')
    ap.add_argument('passcounts')
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    first, rows = load_chains(a.chains)
    vb = load_pass(a.passcounts)
    n = max(len(r[0]) for r in rows)
    sets, totals = [], []
    for s in range(n):
        act = frozenset(i for i, (c, _) in enumerate(rows) if s < len(c) and c[s] > 0)
        sets.append(act)
        totals.append(sum(c[s] for c, _ in rows if s < len(c)))
    ids, idmap = [], {}
    for st in sets:
        if st not in idmap:
            idmap[st] = len(idmap)
        ids.append(idmap[st])
    out = []
    out.append('window swaps %d..%d, %d chains, %d distinct draw sets' % (first, first + n - 1, len(rows), len(idmap)))
    out.append('swap  vblank  dv  set  nchains draws  diff_prev  return(lag,set_before)')
    returns = 0
    last_seen = {}
    prev_vb = None
    for s in range(n):
        g = first + s
        v = vb.get(g, (0, 0, -1))[2]
        dv = (v - prev_vb) if (prev_vb is not None and v >= 0) else 0
        prev_vb = v if v >= 0 else prev_vb
        d = len(sets[s] ^ sets[s - 1]) if s else 0
        ret = ''
        if s >= 2 and ids[s] != ids[s - 1] and ids[s] in last_seen and last_seen[ids[s]] < s - 1:
            ret = 'RETURN lag %d (from set %d)' % (s - last_seen[ids[s]], ids[s - 1])
            returns += 1
        last_seen[ids[s]] = s
        out.append('%5d %7d %3d %4d %7d %5d %9d  %s' % (g, v, dv, ids[s], len(sets[s]), totals[s], d, ret))
    # run-length summary of set ids
    runs, cur, ln = [], ids[0], 0
    for i in ids:
        if i == cur:
            ln += 1
        else:
            runs.append('%d*%d' % (cur, ln)); cur, ln = i, 1
    runs.append('%d*%d' % (cur, ln))
    out.append('')
    out.append('returns: %d' % returns)
    out.append('runs (set*length): ' + ' '.join(runs))
    # chain membership per set id
    out.append('')
    for st, i in sorted(idmap.items(), key=lambda kv: kv[1]):
        out.append('set %d: chains %s' % (i, ' '.join(str(c) for c in sorted(st))))
    out.append('')
    for i, (_, desc) in enumerate(rows):
        out.append('chain %d: %s' % (i, desc))
    with open(a.out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(out) + '\n')
    print('returns', returns, 'distinct sets', len(idmap))


if __name__ == '__main__':
    sys.exit(main())
