#!/usr/bin/env python3
"""ffxiii2-nx (2026-11-03): Renut's hook table (app/src/renut_engine/gpu/d3d/hooks/*.cpp, zumasrevenge-nx's, at Zuma's
Revenge's addresses, XDK 2.0.20871) at Final Fantasy XIII-2's addresses (the same XDK; docs/ffxiii2-nx.md, "Renut's
hooks at FF's addresses").

The map is out/ff_hook_map.txt, one line per Zuma's Revenge address: `ZR FF evidence...` or `ZR - why...` (no FF
function proven). A hook line whose address is mapped gets FF's address everywhere in its code part (comments keep the
addresses they cite); one that is not becomes the matching BANJO_RENUT_UNHOOKED_* macro (banjo_renut_select.h: the
handler compiles, nothing is registered), so the game's own function runs there. Every other sub_X / __imp__sub_X /
renut_native_sub_X naming a mapped address in the hook files is rewritten too. Idempotent: FF addresses are not keys.

  python tools/ffxiii2_renut_hook_remap.py [--dry-run]
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOOKS = os.path.join(ROOT, 'app', 'src', 'renut_engine', 'gpu', 'd3d', 'hooks')
EXTRA = [os.path.join(ROOT, 'app', 'src', 'renut_engine', 'gpu', 'banjo_shared_hooks.h')]
MAP = os.path.join(ROOT, 'out', 'ff_hook_map.txt')
HOOK = re.compile(r'^(\s*)(BANJO_RENUT_HOOK(?:_RAW)?(?:_SHARED)?)\(\s*sub_([0-9A-Fa-f]{8})\b')
DECL = re.compile(r'(?<![0-9A-Za-z])((?:__imp__|renut_native_)?sub_)([0-9A-Fa-f]{8})\b')


def load_map():
    mapping, unmapped = {}, set()
    for line in open(MAP):
        p = line.split()
        if not p or p[0].startswith('#'):
            continue
        z = int(p[0], 16)
        if p[1] == '-':
            unmapped.add(z)
        else:
            mapping[z] = int(p[1], 16)
    return mapping, unmapped


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()
    mapping, unmapped = load_map()
    counts = {'mapped': 0, 'unhooked': 0, 'unknown': 0}
    unknown = []
    files = [os.path.join(HOOKS, f) for f in sorted(os.listdir(HOOKS)) if f.endswith(('.cpp', '.h'))] + EXTRA
    for path in files:
        raw = open(path, encoding='utf-8', newline='').read()
        lines = raw.split('\n')
        for i, line in enumerate(lines):
            cut = line.find('//')
            code, rest = (line, '') if cut < 0 else (line[:cut], line[cut:])
            m = HOOK.match(code)
            if m:
                z = int(m.group(3), 16)
                if z in mapping:
                    counts['mapped'] += 1
                elif z in unmapped or z not in mapping:
                    if z not in unmapped:
                        counts['unknown'] += 1
                        unknown.append('%s: %s' % (os.path.basename(path), line.strip()[:110]))
                    counts['unhooked'] += 1
                    macro = m.group(2).replace('BANJO_RENUT_HOOK', 'BANJO_RENUT_UNHOOKED_HOOK')
                    code = code[:m.start(2)] + macro + code[m.end(2):]
                    rest = rest + ('' if 'ffxiii2-nx: unhooked' in rest else '  // ffxiii2-nx: unhooked (out/ff_hook_map.txt)')
            code = DECL.sub(lambda mm: mm.group(1) + ('%08X' % mapping[int(mm.group(2), 16)]
                                                     if int(mm.group(2), 16) in mapping else mm.group(2)), code)
            lines[i] = code + rest
        out = '\n'.join(lines)
        if out != raw and not a.dry_run:
            open(path, 'w', encoding='utf-8', newline='').write(out)
    for u in unknown:
        print('  not in the map (unhooked): ' + u)
    print('ffxiii2_renut_hook_remap: %(mapped)d hooks mapped, %(unhooked)d unhooked (%(unknown)d not in the map)' % counts)
    return 0


if __name__ == '__main__':
    sys.exit(main())
