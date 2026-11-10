#!/usr/bin/env python3
"""ffxiii2-nx: the direct call graph (bl) of FF's default.xex from the loaded image (out/renut_image.bin) and the
function starts of app/generated/renut_init.cpp. Writes out/ff_calls.json {caller: [callees]}."""
import bisect, json, re, struct
B = 0x82000000
img = open('out/renut_image.bin', 'rb').read()
starts = sorted(int(m, 16) for m in re.findall(r'\{ 0x([0-9A-F]{8}), sub_', open('app/generated/renut_init.cpp').read()))
calls = {}
for i, s in enumerate(starts):
    e = starts[i + 1] if i + 1 < len(starts) else s + 4
    if e - s > 0x40000: e = s + 0x40000
    out = []
    for a in range(s, e, 4):
        w = struct.unpack_from('>I', img, a - B)[0]
        if w >> 26 == 18 and (w & 3) == 1:
            off = w & 0x03FFFFFC
            if off & 0x02000000: off -= 0x04000000
            out.append((a + off) & 0xFFFFFFFF)
    calls['%08X' % s] = ['%08X' % t for t in out]
json.dump(calls, open('out/ff_calls.json', 'w'))
print(len(calls), 'functions')
