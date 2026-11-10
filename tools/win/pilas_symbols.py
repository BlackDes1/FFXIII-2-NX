#!/usr/bin/env python3
"""ffxiii2-nx: names for a stack dump of nfsmw_perfil_pc_pilas_s (app/out/pc/logs/pilas_N.txt): the frames in renut.exe
through llvm-symbolizer and renut.pdb, the others as module+offset. One block per thread.

  python tools/win/pilas_symbols.py app/out/pc/logs/pilas_1.txt > out/pilas_1_named.txt
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SYM = os.path.join(os.path.dirname(ROOT), '_tools', 'llvm', 'bin', 'llvm-symbolizer.exe')
EXE = os.path.join(ROOT, 'app', 'out', 'pc', 'renut.exe')

modules, threads, order = {}, {}, []
for line in open(sys.argv[1], encoding='utf-8', errors='replace'):
    p = line.rstrip('\n').split(';')
    if p[0] == '#modulo':
        modules[p[1]] = p[2]
    elif p[0] == '#hilo':
        order.append(p[1]); threads[p[1]] = (p[2], [])
    elif len(p) == 4 and p[0] in threads:
        threads[p[0]][1].append((p[2], int(p[3], 16)))
renut = [k for k, v in modules.items() if v.lower() == 'renut.exe'][0]
rvas = sorted({o for t in threads.values() for m, o in t[1] if m == renut})
names = {}
if rvas:
    out = subprocess.run([SYM, '--obj=' + EXE, '--relative-address', '--functions=short', '--no-inlines'],
                         input='\n'.join('0x%X' % r for r in rvas) + '\n', capture_output=True, text=True).stdout
    blocks = [b for b in out.split('\n\n') if b.strip()]
    for r, b in zip(rvas, blocks):
        ls = b.strip().split('\n')
        names[r] = ls[0] + ('  ' + os.path.basename(ls[1]) if len(ls) > 1 else '')
for tid in order:
    name, frames = threads[tid]
    print('== thread %s %s' % (tid, name))
    for m, o in frames:
        print('   ' + (names.get(o, '?') if m == renut else '%s+0x%X' % (modules.get(m, m), o)))
