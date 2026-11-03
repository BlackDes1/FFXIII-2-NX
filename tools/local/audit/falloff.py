#!/usr/bin/env python3
"""Functions whose emitted code falls through past its last emitted instruction (no b/blr/bctr/trap), and what lies at
the fall-through address. Also: emitted conditional branches whose target is outside every function."""
import sys, collections
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from common import Mod
img, base, gen = sys.argv[1], int(sys.argv[2], 16), sys.argv[3]
m = Mod(img, base, gen)
def uncond(w):
    op = w >> 26
    if op == 18: return not w & 1
    if op == 19 and ((w >> 1) & 0x3FF) in (16, 528): return ((w >> 21) & 0x14) == 0x14 and not w & 1
    if op == 16: return ((w >> 21) & 0x14) == 0x14 and not w & 1
    if op == 31 and ((w >> 1) & 0x3FF) == 4 and ((w >> 21) & 31) == 31: return True   # tw 31 = trap
    if op == 3 and ((w >> 21) & 31) == 31: return True  # twi 31
    return False
out = collections.Counter(); rows = []
for f, em in m.emit.items():
    s = set(em)
    for a in em:
        w = m.w(a)
        if uncond(w) or (a + 4) in s: continue
        nxt = a + 4
        if nxt in m.reg: kind = 'falls into function start'
        elif m.w(nxt) == 0: kind = 'falls into zero word'
        elif m.owners(nxt) and max(m.owners(nxt)) != f: kind = 'falls into another function body'
        elif m.owners(nxt): kind = 'falls into unemitted code of the same function'
        else: kind = 'falls into a gap'
        out[kind] += 1
        prev_bl = (w >> 26 == 18 and w & 1)
        rows.append((f, a, kind, prev_bl, w))
print(img, dict(out))
for f, a, kind, bl, w in rows:
    pass
    print(f"  0x{f:08X} last 0x{a:08X} {w:08X} {'(bl)' if bl else ''} -> {kind}")
