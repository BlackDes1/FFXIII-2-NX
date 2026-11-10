#!/usr/bin/env python3
"""Print the non-[rtlog] log lines in order, with a marker for each [rtlog] frame in the given ranges.

Usage: log_context.py --ranges 4255-4300,4436-4444 --out file log1 [log2 ...]   (read-only)
"""
import argparse, re, sys

RT = re.compile(r'\[rtlog\] frame (\d+)')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('logs', nargs='+')
    ap.add_argument('--ranges', required=True)
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    ranges = [tuple(int(x) for x in r.split('-')) for r in a.ranges.split(',')]
    out, other = [], 0
    for p in a.logs:
        out.append('=== ' + p)
        with open(p, encoding='utf-8', errors='replace') as f:
            for line in f:
                line = line.rstrip('\n')
                m = RT.search(line)
                if m:
                    fr = int(m.group(1))
                    if any(lo <= fr <= hi for lo, hi in ranges):
                        out.append('RT %d (%d chars)' % (fr, len(line)))
                    continue
                other += 1
                out.append(line[:400])
    with open(a.out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(out) + '\n')
    print('non-rtlog lines', other)


if __name__ == '__main__':
    sys.exit(main())
