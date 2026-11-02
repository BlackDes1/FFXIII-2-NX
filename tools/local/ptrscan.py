#!/usr/bin/env python3
"""Pointer-only function sweep for one module.
1. Function extents from the generated code: start (codegen.partition.json) + 4 * instruction comments.
2. Gaps in the executable sections not covered by any function, ignoring all-zero padding.
3. Candidate entries: 4-aligned words in the non-code sections, and lis/addi (lis/ori) pairs in code, whose value
   lands in a non-padding gap.
4. Each candidate is walked to its unconditional exit (forward conditional branches extend the end); it is accepted
   when the code stays inside the gap, contains no zero word, and (entry check) the word before it is an unconditional
   exit / padding / the end of a function, or the candidate is the gap start.
usage: ptrscan.py IMAGE BASE GENDIR [--toml OUT] """
import glob, json, os, re, struct, sys
img_path, base, gen = sys.argv[1], int(sys.argv[2], 16), sys.argv[3]
toml_out = sys.argv[sys.argv.index("--toml") + 1] if "--toml" in sys.argv else None
img = open(img_path, "rb").read()
def word(a): return struct.unpack_from(">I", img, a - base)[0]
# sections
pe = struct.unpack_from("<I", img, 0x3C)[0]; nsec = struct.unpack_from("<H", img, pe + 6)[0]; opt = struct.unpack_from("<H", img, pe + 20)[0]
code, data = [], []
s = pe + 24 + opt
for i in range(nsec):
    name = img[s:s + 8].split(b"\0")[0].decode(); vs, va = struct.unpack_from("<II", img, s + 8); ch = struct.unpack_from("<I", img, s + 36)[0]
    (code if ch & 0x20000000 else data).append((name, base + va, base + va + vs)); s += 40
data_all = list(data)
data = [d for d in data if d[0] not in (".pdata", ".reloc", ".idata", ".edata", ".XBLD", ".tls")]
# functions
size = {}
named = {m.group(2): int(m.group(1), 16) for m in re.finditer(r'SetFunction\(0x([0-9A-F]{8}), (\w+)\)', open(glob.glob(os.path.join(gen, '*_register.cpp'))[0]).read()) if not m.group(2).startswith('sub_')}
# extent 1: last label + the instructions emitted after it (codegen does not emit blocks it finds unreachable)
for f in glob.glob(os.path.join(gen, "*_recomp.*.cpp")):
    cur = None
    for line in open(f):
        if line.startswith("DEFINE_REX_FUNC("):
            nm = line[16:line.index(")")]
            cur = int(nm[4:12], 16) if nm.startswith("sub_") else named.get(nm)
            if cur is not None: size[cur] = [cur, 0]
        elif cur is None: continue
        elif line.startswith("loc_"):
            size[cur] = [max(size[cur][0], int(line[4:12], 16)), 0]
        elif line.startswith("\t// "):
            size[cur][1] += 4
ends = {a: v[0] + v[1] for a, v in size.items()}
# extent 2: the .pdata entry (start, length in words) for that start, when longer
pd = [d for d in data_all if d[0] == ".pdata"]
for name, lo, hi in pd:
    for o in range(lo, hi - 7, 8):
        b, w = struct.unpack_from(">II", img, o - base)
        if b in ends: ends[b] = max(ends[b], b + ((w >> 8) & 0x3FFFFF) * 4)
registered = {int(m.group(1), 16) for m in re.finditer(r'SetFunction\(0x([0-9A-F]{8}),', open(glob.glob(os.path.join(gen, '*_register.cpp'))[0]).read())}
starts = sorted(set(int(k, 16) for k in json.load(open(os.path.join(gen, "codegen.partition.json")))["assignments"]) | registered)
for a in registered: ends.setdefault(a, a + 16 if word(a + 8) == 0x7D6903A6 else a)   # import stubs: 4 words
funcs = sorted((a, ends.get(a, a)) for a in starts)
fstart = set(starts)
cover = []
for a, e in funcs:
    if cover and a <= cover[-1][1]: cover[-1][1] = max(cover[-1][1], e)
    else: cover.append([a, e])
gaps = []
for name, lo, hi in code:
    p = lo
    for a, e in cover:
        if e <= lo or a >= hi: continue
        if a > p: gaps.append((p, a))
        p = max(p, e)
    if p < hi: gaps.append((p, hi))
def nonpad(g):  # trim zero words at both ends
    a, e = g
    while a < e and word(a) == 0: a += 4
    while e > a and word(e - 4) == 0: e -= 4
    return (a, e) if a < e else None
gaps = [g for g in (nonpad(g) for g in gaps) if g]
gap_starts = [g[0] for g in gaps]
import bisect
def gap_of(x):
    i = bisect.bisect_right(gap_starts, x) - 1
    return gaps[i] if i >= 0 and gaps[i][0] <= x < gaps[i][1] else None
in_code = lambda x: any(lo <= x < hi for _, lo, hi in code)
cands = {}
for name, lo, hi in data:
    for a in range(lo, hi - 3, 4):
        x = word(a)
        if x & 3 or not in_code(x): continue
        if gap_of(x): cands.setdefault(x, []).append(f"pointer stored at 0x{a:08X} ({name})")
for name, lo, hi in code:  # lis rX,hi ; addi/ori rY,rX,lo within 8 instructions
    for a in range(lo, hi - 3, 4):
        w = word(a)
        if w >> 26 != 15 or (w >> 16) & 31 != 0: continue
        rt, hi16 = (w >> 21) & 31, w & 0xFFFF
        for b in range(a + 4, min(a + 36, hi), 4):
            v = word(b); op = v >> 26
            if op in (14, 24) and (v >> 16) & 31 == rt:
                lo16 = v & 0xFFFF
                x = ((hi16 << 16) + (lo16 - 0x10000 if (op == 14 and lo16 & 0x8000) else lo16)) & 0xFFFFFFFF
                if in_code(x) and not x & 3 and gap_of(x): cands.setdefault(x, []).append(f"address built at 0x{a:08X} (lis/{'addi' if op == 14 else 'ori'})")
                break
def walk(t, limit):
    need = t; a = t
    while a < limit:
        w = word(a)
        if w == 0: return None, "zero word"
        op = w >> 26
        if op == 16 and not w & 2:
            bd = w & 0xFFFC; bd = bd - 0x10000 if bd & 0x8000 else bd
            if not (a + bd == limit and limit in fstart): need = max(need, a + bd)   # to the next function: tail call
        if ((op == 18 and not w & 1) or w in (0x4E800020, 0x4E800420)) and a >= need: return a + 4, "ok"
        a += 4
    if a == limit and limit in fstart and need < limit: return a, "ok"   # falls through into the next function
    return None, "runs past the gap"
acc, rej = [], []
for x in sorted(cands):
    g = gap_of(x); end, why = walk(x, g[1])
    prev = word(x - 4)
    pw = prev >> 26
    entry_ok = x == g[0] or prev == 0 or (pw == 18 and not prev & 1) or prev in (0x4E800020, 0x4E800420)
    if end and entry_ok: acc.append((x, end - x, cands[x], g))
    else: rej.append((x, why if not end else "preceded by straight-line code (inside a function body)", cands[x], g))
print(f"code {[(n, hex(l), hex(h)) for n, l, h in code]}; functions {len(funcs)}; non-padding gaps {len(gaps)} ({sum(e - a for a, e in gaps)} bytes)")
print(f"candidates {len(cands)}: accepted {len(acc)}, rejected {len(rej)}")
for x, n, ev, g in acc: print(f"  ACCEPT 0x{x:08X} size {n}  gap 0x{g[0]:08X}-0x{g[1]:08X}  {ev[0]}{' (+%d more)' % (len(ev) - 1) if len(ev) > 1 else ''}")
for x, why, ev, g in rej: print(f"  reject 0x{x:08X} {why}  gap 0x{g[0]:08X}-0x{g[1]:08X}  {ev[0]}")
if toml_out:
    with open(toml_out, "w") as f:
        for x, n, ev, g in acc:
            f.write(f"0x{x:08X} = {{ size = {n} }} # pointer-only: {ev[0]}{' (+%d more)' % (len(ev) - 1) if len(ev) > 1 else ''}; code to the unconditional exit inside .pdata gap 0x{g[0]:08X}-0x{g[1]:08X}\n")
big = sorted(gaps, key=lambda g: g[0] - g[1])[:15]
print("largest gaps:", ", ".join(f"0x{a:08X}+{e - a}" for a, e in big))
if "--prologues" in sys.argv:
    pro = [(a, e) for a, e in gaps if word(a) == 0x7D8802A6]   # mflr r12: an MSVC function prologue at a gap start
    print(f"gaps starting with a prologue (mflr r12): {len(pro)}")
    for a, e in pro: print(f"  0x{a:08X}+{e - a}{'  (pointer candidate)' if a in cands else ''}")
# exact-shape thunks in gaps (reached through computed/virtual pointers the scan cannot see)
if "--thunks" in sys.argv:
    out = []
    for a0, e0 in gaps:
        a = a0
        while a + 8 <= e0:
            w0, w1 = word(a), word(a + 4)
            if a + 16 <= e0 and w0 == 0x81830000 and (w1 & 0xFFFF0000) == 0x816C0000 and word(a + 8) == 0x7D6903A6 and word(a + 12) == 0x4E800420:
                if a not in cands: out.append((a, 16, f"virtual-call thunk: lwz r12,0(r3); lwz r11,{w1 & 0xFFFF}(r12); mtctr r11; bctr"))
                a += 16; continue
            if ((w0 & 0xFFFF0000) in (0x38630000, 0x80630000)) and (w1 >> 26) == 18 and not w1 & 3:
                d = w0 & 0xFFFF; d = d - 0x10000 if d & 0x8000 else d
                bd = w1 & 0x3FFFFFC; bd = bd - 0x4000000 if bd & 0x2000000 else bd
                tgt = a + 4 + bd
                kind = "this-adjustor thunk: addi r3,r3," if (w0 >> 26) == 14 else "member-forwarding thunk: lwz r3,"
                if a not in cands and tgt in fstart: out.append((a, 8, f"{kind}{d}{'' if (w0 >> 26) == 14 else '(r3)'}; b 0x{tgt:08X} (a function)"))
                a += 8; continue
            a += 4
    print(f"exact-shape thunks in gaps, not already pointer candidates: {len(out)}")
    with open(sys.argv[sys.argv.index("--thunks") + 1], "w") as f:
        for a, n, why in out: f.write(f"0x{a:08X} = {{ size = {n} }} # {why}; in a .pdata gap, no direct caller\n")
if "--where" in sys.argv:
    for x in [int(v, 16) for v in sys.argv[sys.argv.index("--where") + 1].split(",")]:
        i = bisect.bisect_right([f[0] for f in funcs], x) - 1
        f = funcs[i]
        print(f"0x{x:08X}: {'function start' if x in fstart else ('inside function 0x%08X-0x%08X' % f if f[0] <= x < f[1] else 'in gap ' + str(gap_of(x)))}")
