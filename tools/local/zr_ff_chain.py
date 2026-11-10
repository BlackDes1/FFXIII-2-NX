#!/usr/bin/env python3
"""ffxiii2-nx: for each Zuma's Revenge hook not in the state object (+0x53E8F0), the delta that puts the longest run of
consecutive ZR hooks (in address order, this one included) on FF function starts. A run of k hooks whose spacing FF
repeats exactly is chance with probability about p^k (p = the share of 4-byte slots that start a function in FF's D3D
area). Output: out/zr_ff_chain.txt."""
import re, bisect, sys
starts = sorted(int(m, 16) for m in re.findall(r'\{ 0x([0-9A-F]{8}), sub_', open('app/generated/renut_init.cpp').read()))
S = set(starts)
rows = [l.split(None, 4) for l in open('out/zr_ff_hooks.txt')]
names = {int(r[0], 16): r[3] for r in rows}
state = {int(r[0], 16): int(r[1], 16) for r in rows if 'state table' in r[4]}
zr = sorted(names)
lo, hi = 0x82840000, 0x829A0000
dens = sum(1 for s in starts if lo <= s < hi) / ((hi - lo) / 4)
print('start density per 4-byte slot in FF 0x82840000-0x829A0000: %.4f' % dens)
# The state object (ZR 0x8230AFC8-0x8230D8A8 at +0x53E8F0: 225 hooks in a row, and the state tables agree) is
# taken: its FF range is no candidate for another hook, and its hooks are not part of another run. Its setters are
# uniform in size, which gave false runs of 6-9 into it at other deltas (12 FF functions claimed twice).
OBJ0, OBJ1, OBJD = 0x8230AFC8, 0x8230D8A8, 0x53E8F0
cands = [s for s in starts if lo <= s < hi and not (OBJ0 + OBJD <= s <= OBJ1 + OBJD + 0x40)]
zr = [a for a in zr if not (OBJ0 <= a <= OBJ1)]
out = []
S_ok = None
out += [(a, a + OBJD, 225, OBJD, OBJ0, OBJ1) for a in sorted(names) if OBJ0 <= a <= OBJ1]
for i, a in enumerate(zr):
    best = (0, 0, 0, 0)
    for s in cands:
        d = s - a
        j = i
        while j + 1 < len(zr) and zr[j + 1] + d in S and zr[j + 1] - zr[j] < 0x2000: j += 1
        k = i
        while k - 1 >= 0 and zr[k - 1] + d in S and zr[k] - zr[k - 1] < 0x2000: k -= 1
        run = j - k + 1
        if run > best[0]: best = (run, d, k, j)
    run, d, k, j = best
    out.append((a, a + d, run, d, zr[k], zr[j]))
with open('out/zr_ff_chain.txt', 'w') as f:
    for a, b, run, d, z0, z1 in out:
        f.write('%08X %08X run %2d delta %06X (%08X-%08X) %s%s\n' % (a, b, run, d, z0, z1, names[a],
                '  [state %08X]' % state[a] if a in state else ''))
