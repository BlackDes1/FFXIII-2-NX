import sys, pickle, collections
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from common import Mod
img, base, gen, pkl = sys.argv[1], int(sys.argv[2], 16), sys.argv[3], sys.argv[4]
m = Mod(img, base, gen)
rest = pickle.load(open(pkl, 'rb'))
term = lambda w: (w >> 26 == 18 and not w & 1) or w in (0x4E800020, 0x4E800420)
rows = []
for x, f, em, lab, sw, ev in rest:
    ev = [e for e in ev if e[0] != 'lis']
    if not ev: continue
    round_ = (x & 0xFFFF) == 0 or (x & 0xFFF) == 0
    prevterm = term(m.w(x - 4))
    rows.append((x, f, em, lab, round_, prevterm, ev))
c = collections.Counter((r[4], r[3], r[5]) for r in rows)
print("(round, label, prev-terminal) ->", c)
for x, f, em, lab, round_, prevterm, ev in rows:
    if round_: continue
    ctx = []
    for e in ev[:2]:
        a = e[1]
        ctx.append(f"{e[2]}@{a:08X}[" + " ".join("%08X" % m.w(a + d) for d in range(-8, 12, 4)) + "]")
    print(f"0x{x:08X} f=0x{f:08X}..{m.end[f]:08X} em={int(em)} lab={int(lab)} prevT={int(prevterm)} n={len(ev)} {' '.join(ctx)}")
