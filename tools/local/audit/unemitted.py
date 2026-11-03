import sys, collections
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from common import Mod
img, base, gen = sys.argv[1], int(sys.argv[2], 16), sys.argv[3]
m = Mod(img, base, gen)
runs = []
for f in sorted(m.reg):
    if f not in m.emit: continue
    s = set(m.emit[f]); cur = None
    for a in range(f, m.end[f], 4):
        w = m.w(a)
        un = a not in m.emitset and w != 0 and not (f <= w < m.end[f]) and not (w in m.reg)  # skip table entries / handler words
        if un:
            if cur and cur[1] == a: cur[1] = a + 4
            else:
                cur = [a, a + 4, f]; runs.append(cur)
c = collections.Counter()
for a, e, f in runs:
    pw = m.w(a - 4); prev_emitted = (a - 4) in set(m.emit[f])
    op = pw >> 26
    kind = 'after bl' if (op == 18 and pw & 1 and prev_emitted) else ('after terminal' if ((op == 18 and not pw & 1) or pw in (0x4E800020, 0x4E800420)) else ('after table/data' if not prev_emitted else 'after straight-line emitted'))
    c[kind] += 1
    if kind in ("after bl", "after straight-line emitted") or "-v" in sys.argv: print(f"  0x{a:08X}-0x{e:08X} ({(e-a)//4} words) in 0x{f:08X}: {kind}, prev {pw:08X}")
print(img, 'unemitted code runs:', len(runs), dict(c))
