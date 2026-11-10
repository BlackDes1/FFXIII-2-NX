#!/usr/bin/env python3
"""grep over source trees. Usage: grep_src.py PATTERN out.txt root [root ...] (*.cpp *.h *.py *.ps1). Read-only."""
import os, re, sys

pat, dst, *roots = sys.argv[1:]
rx = re.compile(pat)
out = []
for root in roots:
    for d, _, files in os.walk(root):
        if '_to_delete' in d or 'generated' in d:
            continue
        for fn in files:
            if not fn.endswith(('.cpp', '.h', '.py', '.ps1', '.toml')):
                continue
            p = os.path.join(d, fn)
            try:
                with open(p, encoding='utf-8', errors='replace') as f:
                    for i, line in enumerate(f, 1):
                        if rx.search(line):
                            out.append('%s:%d: %s' % (p, i, line.rstrip()[:300]))
            except OSError:
                pass
with open(dst, 'w', encoding='utf-8') as f:
    f.write('\n'.join(out) + '\n')
print(len(out), 'matches')
