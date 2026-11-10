#!/usr/bin/env python3
"""grep for UTF-16 or UTF-8 job logs. Usage: grep_logs.py PATTERN out.txt file [file ...] (globs ok). Read-only."""
import glob, re, sys

pat, dst, *files = sys.argv[1:]
rx = re.compile(pat)
out = []
for spec in files:
    for p in sorted(glob.glob(spec)):
        raw = open(p, 'rb').read()
        if raw[:2] in (b'\xff\xfe', b'\xfe\xff') or raw[1:2] == b'\x00':
            text = raw.decode('utf-16', errors='replace')
        else:
            text = raw.decode('utf-8', errors='replace')
        for line in text.splitlines():
            if rx.search(line):
                out.append('%s: %s' % (p.split('\\')[-1], line[:1500]))
with open(dst, 'w', encoding='utf-8') as f:
    f.write('\n'.join(out) + '\n')
print(len(out), 'matches')
