#!/usr/bin/env python3
"""ffxiii2-nx: pair zumasrevenge-nx's Renut hook addresses (Zuma's Revenge, XDK 2.0.20871) with Final Fantasy XIII-2's
(the same XDK). The D3D library's object files sit at different offsets in the two games; inside one object the
functions keep their spacing. For each hook, every delta that puts it on an FF function start is scored by how many
of its neighbours (ZR hooks within WINDOW bytes) the same delta also puts on FF function starts with the same
spacing to the next function (function size). State setters come from the two state tables (row = D3DRS / 4).
Output: out/zr_ff_hooks.txt (zr ff score name)."""
import bisect, os, re, struct, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ZR = sys.argv[1]  # zumasrevenge-nx checkout
WINDOW = 0x1800
starts = sorted(int(m, 16) for m in re.findall(r'\{ 0x([0-9A-F]{8}), sub_', open(os.path.join(ROOT, 'app/generated/renut_init.cpp')).read()))
sset = set(starts)
def size(a):
    i = bisect.bisect_right(starts, a)
    return starts[i] - a if i < len(starts) else 0
HOOK = re.compile(r'^\s*BANJO_RENUT_HOOK(?:_RAW|_SHARED)?\(\s*sub_([0-9A-Fa-f]{8})\s*,\s*([^)]*)\);\s*//\s*(\S+)')
hooks = []
hd = os.path.join(ZR, 'app/src/renut_engine/gpu/d3d/hooks')
for f in sorted(os.listdir(hd)):
    if not f.endswith('.cpp'): continue
    dis = 0
    for line in open(os.path.join(hd, f)):
        if line.startswith('#if'): dis += 1 if (dis or line.startswith('#if 0')) else 0; continue
        if line.startswith('#endif'): dis = max(0, dis - 1); continue
        m = HOOK.match(line)
        if m and not dis: hooks.append((int(m.group(1), 16), m.group(2).strip(), m.group(3), f))
img = open(os.path.join(ROOT, 'out/renut_image.bin'), 'rb').read()
def table(t, n): return [struct.unpack_from('>3I', img, t - 0x82000000 + 12 * i) for i in range(n)]
RS, SS = table(0x82CBB508, 101), table(0x82CBB9C8, 20)
zr_addrs = sorted(set(h[0] for h in hooks))
# ZR function sizes are unknown; the spacing between consecutive ZR hooks stands in for them.
out = []
for a, tmpl, name, f in hooks:
    m = re.match(r'Set(Render|Sampler)StateHook<(\d+)>', tmpl)
    if m:
        row = int(m.group(2)); t = RS if m.group(1) == 'Render' else SS
        out.append((a, t[row][1], 99, name, 'state table row %d' % row)); continue
    nb = [b for b in zr_addrs if abs(b - a) <= WINDOW and b != a]
    best = []
    lo = bisect.bisect_left(starts, a - 0x01000000)
    for d in set(s - a for s in starts if abs(s - a) < 0x900000 and 0x82800000 <= s < 0x82A00000):
        score = sum(1 for b in nb if b + d in sset)
        best.append((score, -abs(d), d))
    best.sort(reverse=True)
    if not best: out.append((a, 0, -1, name, 'none')); continue
    s0, _, d0 = best[0]
    s1 = best[1][0] if len(best) > 1 else -1
    out.append((a, a + d0, s0, name, 'delta %X, %d/%d neighbours, next %d' % (d0, s0, len(nb), s1)))
with open(os.path.join(ROOT, 'out/zr_ff_hooks.txt'), 'w') as fo:
    for a, b, s, n, e in out: fo.write('%08X %08X %3d %-50s %s\n' % (a, b, s, n, e))
print(len(out), 'hooks')
