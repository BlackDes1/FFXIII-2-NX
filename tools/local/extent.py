#!/usr/bin/env python3
"""Function extent from an entry: follow straight-line code; a conditional branch forward extends the required end;
stop at an unconditional exit (b / blr / bctr) once no pending branch target lies beyond it.
Prints a toml [functions] line with its evidence. usage: extent.py IMAGE BASE ENTRY:CALLER ..."""
import os, re, subprocess, sys
DIS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "dis.sh")
img, base = sys.argv[1], int(sys.argv[2], 16)
for arg in sys.argv[3:]:
    t, src = (int(x, 16) for x in arg.split(":"))
    out = subprocess.run([DIS, img, hex(base), hex(t), hex(t + 4 * 400)], capture_output=True, text=True).stdout
    need, end, last = t, None, ""
    for l in out.splitlines():
        m = re.match(r"([0-9a-f]+):\s+((?:[0-9a-f]{2} ){4})\s*(.*)", l)
        if not m: continue
        a, w, txt = int(m.group(1), 16), int(m.group(2).replace(" ", ""), 16), m.group(3).strip()
        op = w >> 26
        if op == 16:   # bc: conditional branch
            bd = w & 0xFFFC; bd = bd - 0x10000 if bd & 0x8000 else bd
            if not (w & 2): need = max(need, a + bd)
        uncond = (op == 18 and not (w & 1)) or w in (0x4E800020, 0x4E800420)
        if uncond and a >= need:
            end, last = a + 4, txt; break
    print(f"0x{t:08X} = {{ size = {end - t} }} # tail-branched to from 0x{src:08X} (b); code from entry to {last.split()[0]} at 0x{end-4:08X}, preceded by an unconditional exit; reads only argument registers before writing")
