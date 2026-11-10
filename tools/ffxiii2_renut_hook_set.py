#!/usr/bin/env python3
"""ffxiii2-nx: turn unhooked Renut hooks (BANJO_RENUT_UNHOOKED_*(sub_<ZR>, ...)) into hooks at FFXIII-2 addresses
proven since the remap (out/ff_hook_map.txt lines `ZR FF evidence`): run after editing the map.

  python tools/ffxiii2_renut_hook_set.py
"""
import os, re
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOOKS = os.path.join(ROOT, 'app', 'src', 'renut_engine', 'gpu', 'd3d', 'hooks')
m = {}
for l in open(os.path.join(ROOT, 'out', 'ff_hook_map.txt')):
    p = l.split()
    if p and not p[0].startswith('#') and p[1] != '-':
        m[p[0].upper()] = p[1].upper()
n = 0
for f in sorted(os.listdir(HOOKS)):
    if not f.endswith('.cpp'): continue
    path = os.path.join(HOOKS, f)
    s = open(path, encoding='utf-8', newline='').read()
    def sub(mm):
        global n
        z = mm.group(2).upper()
        if z not in m: return mm.group(0)
        n += 1
        return 'BANJO_RENUT_HOOK%s(sub_%s' % (mm.group(1), m[z])
    t = re.sub(r'BANJO_RENUT_UNHOOKED_HOOK(_RAW|_SHARED|)\(sub_([0-9A-Fa-f]{8})', sub, s)
    t = t.replace('// ffxiii2-nx: unhooked (out/ff_hook_map.txt)', '')  # stale on re-hooked lines only below
    if t != s:
        # keep the unhooked note on lines still unhooked
        lines = t.split('\n'); orig = s.split('\n')
        for i, line in enumerate(lines):
            if 'BANJO_RENUT_UNHOOKED_' in line and '// ffxiii2-nx: unhooked' in orig[i]:
                lines[i] = orig[i]
            elif 'BANJO_RENUT_HOOK' in line and line != orig[i] and 'BANJO_RENUT_UNHOOKED_' in orig[i]:
                lines[i] = line.rstrip() + '  // ffxiii2-nx: the D3D trace (out/ff_hook_map.txt)'
        open(path, 'w', encoding='utf-8', newline='').write('\n'.join(lines))
print('%d hooks set' % n)
