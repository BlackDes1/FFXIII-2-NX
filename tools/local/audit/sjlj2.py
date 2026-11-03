#!/usr/bin/env python3
"""setjmp/longjmp-like code anywhere in the code ranges, any base register:
 - windows of 64 words with >= 10 stores of distinct non-volatile GPRs (r13-r31, std/stw) or FPRs (f14-f31, stfd)
   into one base register other than r1/r12 (stack + save/restore helpers);
 - the mirror: >= 10 loads of distinct non-volatile registers from one base other than r1/r12;
 - every load of r1 from a base other than r1 (stack pointer restore: longjmp / context switch), and every store of
   r1 into a base other than r1 (context capture)."""
import sys, collections
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from common import Mod
img, base, gen = sys.argv[1], int(sys.argv[2], 16), sys.argv[3]
m = Mod(img, base, gen)
import bisect
def fn_of(a):
    i = bisect.bisect_right(m.starts, a) - 1
    return m.starts[i] if i >= 0 else None
hits = collections.defaultdict(lambda: collections.defaultdict(set))
r1ops = []
for n, lo, hi in m.code:
    for a in range(lo, hi, 4):
        w = m.w(a); op, rs, ra = w >> 26, (w >> 21) & 31, (w >> 16) & 31
        st = (op == 62 and w & 3 == 0) or op == 36   # std / stw
        ld = (op == 58 and w & 3 == 0) or op == 32   # ld / lwz
        if ra in (1, 12): pass
        elif st and rs >= 13: hits[('st', ra)][a >> 8].add('r%d' % rs)
        elif op == 54 and rs >= 14: hits[('st', ra)][a >> 8].add('f%d' % rs)
        elif ld and rs >= 13 and rs != ra: hits[('ld', ra)][a >> 8].add('r%d' % rs)
        elif op == 50 and rs >= 14: hits[('ld', ra)][a >> 8].add('f%d' % rs)
        if rs == 1 and ra != 1 and (st or ld) and a in m.emitset | set():
            r1ops.append((a, 'store r1 ->' if st else 'load r1 <-', ra, (w & 0xFFFF) - (0x10000 if w & 0x8000 else 0)))
        elif rs == 1 and ra != 1 and (st or ld):
            r1ops.append((a, ('store r1 ->' if st else 'load r1 <-') + ' (not emitted)', ra, (w & 0xFFFF) - (0x10000 if w & 0x8000 else 0)))
seen = set()
for (k, ra), pages in hits.items():
    for p, regs in pages.items():
        allr = regs | pages.get(p + 1, set())
        if len(allr) >= 10:
            a = p << 8
            f = fn_of(a)
            if (k, ra, f) in seen: continue
            seen.add((k, ra, f))
            print(f"{k} of {len(allr)} non-volatile regs via r{ra} near 0x{a:08X} (function 0x{f:08X} {m.reg.get(f)})")
print(f"r1 loaded/stored through another base: {len(r1ops)}")
for a, what, ra, d in r1ops:
    f = fn_of(a)
    print(f"  0x{a:08X} {what} {d}(r{ra})  in 0x{f:08X} {m.reg.get(f)}")
