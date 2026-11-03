#!/usr/bin/env python3
"""All code addresses the module takes as values (data words, words in code not emitted as instructions, lis+addi/ori
pairs), classified: registered start / inside a function (emitted? label? switch case?) / gap."""
import sys, struct, bisect, collections
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from common import Mod
img, base, gen = sys.argv[1], int(sys.argv[2], 16), sys.argv[3]
m = Mod(img, base, gen)
lim = base + len(m.img)
cands = collections.defaultdict(list)
for n, lo, hi, ch in m.secs:
    if ch & 0x20000000 or n in ('.pdata', '.reloc'): continue
    for a in range(lo, min(hi, lim) - 3, 4):
        x = m.w(a)
        if not x & 3 and m.in_code(x): cands[x].append(('data', a, n))
for n, lo, hi in m.code:
    for a in range(lo, hi - 3, 4):
        w = m.w(a)
        if a not in m.emitset:
            if not w & 3 and m.in_code(w): cands[w].append(('codeword', a, n))
        if w >> 26 != 15 or (w >> 16) & 31 != 0: continue
        rt, hi16 = (w >> 21) & 31, w & 0xFFFF
        for b in range(a + 4, min(a + 64, hi), 4):
            v = m.w(b); op = v >> 26
            if op in (14, 24) and (v >> 16) & 31 == rt:
                lo16 = v & 0xFFFF
                x = ((hi16 << 16) + (lo16 - 0x10000 if (op == 14 and lo16 & 0x8000) else lo16)) & 0xFFFFFFFF
                if m.in_code(x) and not x & 3: cands[x].append(('lis', a, b))
                break
            # stop if rt redefined
            if (v >> 21) & 31 == rt and op in (14, 15, 24, 32, 31, 21): break
starts = set(m.reg)
res = collections.Counter(); mids = []; gaps = []
for x, ev in sorted(cands.items()):
    if x in starts: res['start'] += 1; continue
    ow = m.owners(x)
    if ow:
        f = max(ow)
        mids.append((x, f, x in m.emitset, x in m.labels.get(f, ()), x in m.swt.get(f, ()), ev))
        res['mid'] += 1
    else:
        gaps.append((x, ev)); res['gap'] += 1
print(f"{img}: targets {len(cands)}: {dict(res)}")
import pickle
pickle.dump((mids, gaps), open(sys.argv[4], 'wb'))
