#!/usr/bin/env python3
import sys, pickle, collections, struct
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from common import Mod
img, base, gen, pkl = sys.argv[1], int(sys.argv[2], 16), sys.argv[3], sys.argv[4]
m = Mod(img, base, gen)
mids, gaps = pickle.load(open(pkl, 'rb'))
lim = base + len(m.img)
# handlers of .pdata entries with the exception bit: two words before the function
seh_words = {}   # location -> role
scope_tables = {}
bad = []
handlers = collections.Counter()
for n, lo, hi, ch in m.secs:
    if n != '.pdata': continue
    for o in range(lo, hi - 7, 8):
        b, w = struct.unpack_from(">II", m.img, o - base)
        if not b or not (w >> 31): continue   # bit31: exception flag
        h, d = m.w(b - 8), m.w(b - 4)
        handlers[(h, m.reg.get(h, '?'))] += 1
        if m.reg.get(h, '').endswith('__C_specific_handler') and base <= d < lim:
            cnt = m.w(d)
            if cnt > 200: bad.append((b, 'scope count', cnt)); continue
            scope_tables[d] = (b, cnt)
            seh_words[d] = 'count'
            for i in range(cnt):
                r = d + 4 + 16 * i
                beg, end, hnd, tgt = m.w(r), m.w(r + 4), m.w(r + 8), m.w(r + 12)
                seh_words[r] = 'begin'; seh_words[r + 4] = 'end'; seh_words[r + 8] = 'handler/filter'; seh_words[r + 12] = 'jumptarget'
                if hnd > 1 and hnd not in m.reg: bad.append((b, 'scope handler/filter not registered', hex(hnd), 'target', hex(tgt)))
                if not (b <= beg <= end <= m.end.get(b, b)): bad.append((b, 'scope range outside function', hex(beg), hex(end)))
print("exception handlers used:", {f"0x{h:08X} {nm}": c for (h, nm), c in handlers.items()})
print(f"scope tables {len(scope_tables)}, records {sum(c for _, c in scope_tables.values())}; problems: {len(bad)}")
for x in bad[:40]: print("  BAD", [hex(v) if isinstance(v, int) else v for v in x])
cls = collections.Counter(); rest = []
for x, f, em, lab, sw, ev in mids:
    roles = set()
    for e in ev:
        if e[0] in ('data', 'codeword') and e[1] in seh_words: roles.add('seh-' + seh_words[e[1]])
        elif e[0] in ('data', 'codeword'): roles.add('data')
        else: roles.add('lis')
    if sw: roles.add('switchcase')
    key = ' + '.join(sorted(roles))
    cls[key] += 1
    if not (roles <= {'seh-begin', 'seh-end', 'seh-jumptarget'}) and 'switchcase' not in roles:
        rest.append((x, f, em, lab, sw, ev))
for k, v in cls.most_common(): print(f"  {v:6d}  {k}")
pickle.dump(rest, open(pkl + '.rest', 'wb'))
print("unexplained mids:", len(rest))
