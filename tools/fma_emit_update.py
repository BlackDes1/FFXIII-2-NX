#!/usr/bin/env python3
"""ffxiii2-nx k236 (D-009): rewrite already generated code to what the updated codegen emits for vmaddfp / vnmsubfp
(sdk/src/codegen/builders/vector.cpp): rex::ppc::rex_vmaddfp / rex_vnmsubfp (fused, rex/ppc/intrinsics.h) instead of
a multiply and an add. Same result as a fresh codegen for these two instructions; nothing else is touched.

usage: fma_emit_update.py <file or dir> ...   (dirs: renut_recomp.*.cpp and hot/*.h / *.inc inside)
"""
import os
import re
import sys

L = r"simde_mm_load_ps\(([\w.\[\]]+)\.f32\)"
MADD = re.compile(r"simde_mm_add_ps\(simde_mm_mul_ps\(" + L + r", " + L + r"\), " + L + r"\)")
NMSUB = re.compile(r"simde_mm_xor_ps\(simde_mm_sub_ps\(simde_mm_mul_ps\(" + L + r", " + L + r"\), " + L +
                   r"\), simde_mm_castsi128_ps\(simde_mm_set1_epi32\(int\(0x80000000\)\)\)\)")
MADD_TO = r"rex::ppc::rex_vmaddfp(simde_mm_load_ps(\1.f32), simde_mm_load_ps(\2.f32), simde_mm_load_ps(\3.f32))"
NMSUB_TO = r"rex::ppc::rex_vnmsubfp(simde_mm_load_ps(\1.f32), simde_mm_load_ps(\2.f32), simde_mm_load_ps(\3.f32))"


def targets(arg):
    if os.path.isfile(arg):
        yield arg
        return
    for root, _, names in os.walk(arg):
        for n in sorted(names):
            if (n.startswith("renut_recomp.") and n.endswith(".cpp")) or (
                    os.path.basename(root) == "hot" and (n.endswith(".h") or n.endswith(".inc"))):
                yield os.path.join(root, n)


def main():
    total_a = total_b = files = 0
    for arg in sys.argv[1:]:
        for path in targets(arg):
            with open(path, encoding="utf-8", newline="") as fh:
                text = fh.read()
            new, a = MADD.subn(MADD_TO, text)
            new, b = NMSUB.subn(NMSUB_TO, new)
            if a or b:
                with open(path, "w", encoding="utf-8", newline="") as fh:
                    fh.write(new)
                files += 1
                total_a += a
                total_b += b
    print(f"{files} files: vmaddfp {total_a}, vnmsubfp {total_b}")


if __name__ == "__main__":
    main()
