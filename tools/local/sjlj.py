#!/usr/bin/env python3
"""setjmp/longjmp by shape: for every function start, count within its first 120 words the stores of non-volatile
GPRs/FPRs into r3 (std rN,d(r3) N>=13 / stfd fN,d(r3) N>=14 / stw r1|r2,d(r3)) and the mirrored loads (ld / lfd from r3).
usage: sjlj.py IMAGE BASE GENDIR"""
import glob, re, struct, sys, os
img_path, base, gen = sys.argv[1], int(sys.argv[2], 16), sys.argv[3]
img = open(img_path, "rb").read()
W = lambda a: struct.unpack_from(">I", img, a - base)[0]
starts = sorted({int(m.group(1), 16) for m in re.finditer(r"SetFunction\(0x([0-9A-F]{8}),", open(glob.glob(os.path.join(gen, "*_register.cpp"))[0]).read())})
for s in starts:
    st = ld = 0; regs_st = set(); regs_ld = set(); mflr = mfcr = mtlr = False
    for a in range(s, s + 480, 4):
        try: w = W(a)
        except struct.error: break
        op, rs, ra = w >> 26, (w >> 21) & 31, (w >> 16) & 31
        if ra == 3:
            if (op == 62 and w & 3 == 0 and rs >= 13) or (op == 54 and rs >= 14) or (op in (36, 62) and rs == 1):
                st += 1; regs_st.add(("f" if op == 54 else "r") + str(rs))
            if (op == 58 and w & 3 == 0 and rs >= 13) or (op == 50 and rs >= 14) or (op in (32, 58) and rs == 1):
                ld += 1; regs_ld.add(("f" if op == 50 else "r") + str(rs))
        if w in (0x4E800020,) : break
    if st >= 12 or ld >= 12:
        print(f"0x{s:08X}: stores into r3 {st} ({len(regs_st)} regs), loads from r3 {ld} ({len(regs_ld)} regs){'  r1 saved' if 'r1' in regs_st else ''}{'  r1 restored' if 'r1' in regs_ld else ''}")
