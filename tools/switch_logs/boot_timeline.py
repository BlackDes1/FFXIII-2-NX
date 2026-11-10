#!/usr/bin/env python3
"""ffxiii2-nx (2026-11-08): boot timeline of console renut_*.log files (the user: newer builds take longer to boot and
the "reading storage device" animation is less fluid than in the first builds).
    python tools/switch_logs/boot_timeline.py LOG [LOG ...]
Per log: seconds from the first line to the DKSH cache load, the device, the game_root mount, and the first 10 s blocks
(game fps, render thread ms, GPU ms, uam compiles so far), plus the lines with 'SLOW' or 'pipelines' in the first 90 s."""
import re
import sys
from datetime import datetime

TS = re.compile(r'^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+)\]')
BLOCK = re.compile(r'\[renut\] ([\d.]+) s: (\d+) game frames \(([\d.]+) fps\), render thread busy ([\d.]+) ms a frame.*?GPU ([\d.]+) ms')
UAM = re.compile(r'uam (\d+) compiles \(avg (\d+) ms')
MARKS = [('dksh', 'DKSH cache /'), ('device', '[deko3d] device: creating'), ('mount', "mounted '/switch/ffxiii2-nx/game_root'"),
         ('enh', '[enhancements]')]


def t(line):
    m = TS.match(line)
    return datetime.strptime(m.group(1), '%Y-%m-%d %H:%M:%S.%f') if m else None


def main():
    for path in sys.argv[1:]:
        lines = open(path, encoding='utf-8', errors='replace').read().splitlines()
        t0 = None
        out = []
        blocks = []
        slow = 0
        for ln in lines:
            tt = t(ln)
            if tt is None:
                continue
            if t0 is None:
                t0 = tt
            s = (tt - t0).total_seconds()
            for name, key in MARKS:
                if key in ln and not any(o[0] == name for o in out):
                    extra = ''
                    if name == 'dksh':
                        m = re.search(r': (\d+) variants', ln)
                        extra = m.group(1) + ' variants' if m else ''
                    out.append((name, s, extra))
            m = BLOCK.search(ln)
            if m and len(blocks) < 9:
                blocks.append((s, float(m.group(3)), float(m.group(4)), float(m.group(5))))
            u = UAM.search(ln)
            if u and blocks and len(blocks[-1]) == 4:
                blocks[-1] = blocks[-1] + (int(u.group(1)),)
            if 'SLOW' in ln and s < 90:
                slow += 1
        print('==', path)
        print('   ' + ', '.join(f'{n} {s:.1f}s {e}'.strip() for n, s, e in out) + f'; SLOW io lines in 90 s: {slow}')
        for b in blocks:
            print('   t=%5.1f  fps %5.2f  render %6.2f ms  gpu %6.2f ms  uam %s' % (b[0], b[1], b[2], b[3], b[4] if len(b) > 4 else '-'))


if __name__ == '__main__':
    main()
