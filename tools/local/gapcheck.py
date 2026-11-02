#!/usr/bin/env python3
"""For each branch target 'not in any function': walk from the target to its first unconditional exit (b / blr / bctr),
report the code, the word before it (the end of whatever precedes), and register reads of r0/r11/r12 before any write
(a sign of a fragment rather than a function entry)."""
import os, re, subprocess, sys
DIS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "dis.sh")
img, base = sys.argv[1], int(sys.argv[2], 16)
def dis(s, e):
    out = subprocess.run([DIS, img, hex(base), hex(s), hex(e)], capture_output=True, text=True).stdout.splitlines()
    res = []
    for l in out:
        m = re.match(r"([0-9a-f]+):\s+((?:[0-9a-f]{2} ){4})\s*(.*)", l)
        if m: res.append((int(m.group(1), 16), m.group(2).replace(" ", ""), m.group(3).strip()))
    return res
def terminal(word, text):
    w = int(word, 16); op = w >> 26
    if op == 18 and not (w & 1): return True            # b / ba (not bl)
    if w in (0x4E800020, 0x4E800420): return True        # blr / bctr
    return False
for line in sys.stdin:
    m = re.search(r"(0x[0-9A-F]{8}) from (0x[0-9A-F]{8})", line)
    if not m: continue
    t, src = int(m.group(1), 16), int(m.group(2), 16)
    ins = dis(t - 8, t + 4 * 64)
    before = [i for i in ins if i[0] < t]
    body = []
    for a, w, txt in ins:
        if a < t: continue
        body.append((a, w, txt))
        if terminal(w, txt): break
    size = body[-1][0] + 4 - t
    written, reads = set(), []
    for a, w, txt in body:
        mm = re.match(r"(\S+)\s+(.*)", txt); 
        if not mm: continue
        ops = [o.strip() for o in mm.group(2).split(",")]
        regs = [int(x) for o in ops for x in re.findall(r"^(\d+)$|\((\d+)\)", o) for x in x if x]
        dst = regs[:1] if mm.group(1).startswith(("l", "add", "mr", "li", "lis", "or", "rlw", "subf", "ext", "neg", "mf", "clr", "slw", "srw")) and not mm.group(1).startswith(("lfs", "lfd")) else []
        for r in regs[len(dst):] + ([] if dst else regs):
            if r in (0, 11, 12) and r not in written and not mm.group(1).startswith("st"): reads.append((hex(a), txt))
        written.update(dst)
    prev = before[-1] if before else None
    print(f"\n### 0x{t:08X} (from b at 0x{src:08X})  size={size}  prev: {prev[2] if prev else '?'}  r0/r11/r12 reads before write: {reads or 'none'}")
    for a, w, txt in body: print(f"   {a:08x}: {w}  {txt}")
