import sys, pickle, collections
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from common import Mod
img, base, gen, pkl = sys.argv[1], int(sys.argv[2], 16), sys.argv[3], sys.argv[4]
m = Mod(img, base, gen)
rest = pickle.load(open(pkl, 'rb'))
def use_kind(a, b):
    """lis at a, addi at b: how is the register used afterwards"""
    rt = (m.w(b) >> 21) & 31
    for c in range(b + 4, b + 48, 4):
        v = m.w(c); op = v >> 26
        if op == 31:
            xo = (v >> 1) & 0x3FF; ra, rb = (v >> 16) & 31, (v >> 11) & 31
            if xo in (23, 87, 279) and rt in (ra, rb): return {23: 'lwzx', 87: 'lbzx', 279: 'lhzx'}[xo]
            if xo == 266 and rt in (ra, rb): return 'add'
        if (v >> 16) & 31 == rt and op in (32, 34, 40): return 'load-disp'
        if v in (0x4E800020, 0x4E800420) or op == 18: break
    return 'other'
dataloc = collections.defaultdict(list); lisrest = []
k = collections.Counter()
for x, f, em, lab, sw, ev in rest:
    for e in ev:
        if e[0] == 'lis':
            u = use_kind(e[1], e[2]); k['lis-' + u] += 1
            if u not in ('lwzx', 'lbzx', 'lhzx', 'add'): lisrest.append((x, f, em, lab, e, u))
        else:
            dataloc[e[1]] = (x, f, em, lab, e)
            k[e[0]] += 1
print(k)
print("lis not table/offset-base:", len(lisrest))
for x, f, em, lab, e, u in lisrest: print(f"  0x{x:08X} in 0x{f:08X} emitted={int(em)} label={int(lab)} built at 0x{e[1]:08X} use={u}")
# data runs
locs = sorted(dataloc); runs = []
for a in locs:
    if runs and a - runs[-1][-1] <= 8: runs[-1].append(a)
    else: runs.append([a])
print("data refs:", len(locs), "runs:", len(runs))
for r in runs:
    x, f, em, lab, e = dataloc[r[0]]
    fs = set(dataloc[a][1] for a in r)
    print(f"  run @0x{r[0]:08X} ({e[2]}) n={len(r)} funcs={','.join('%08X' % q for q in sorted(fs))[:60]} first target 0x{x:08X} emitted={int(em)} label={int(lab)}")
