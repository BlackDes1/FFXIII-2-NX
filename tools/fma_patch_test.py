#!/usr/bin/env python3
"""ffxiii2-nx k236 test (D-009): make vmaddfp / vnmsubfp fused (one rounding, as the Xenon's VMX does) in the
listed generated functions only. The codegen emits mul then add (two roundings).

usage: fma_patch_test.py <generated dir> <apply|revert> sub_XXXXXXXX ...
apply keeps <file>.before_fma copies; revert restores them.
"""
import os
import re
import sys

HELPER = (
    "\n// ffxiii2-nx k236 test: fused multiply-add per lane (tools/fma_patch_test.py)\n"
    "#include <cmath>\n"
    "static inline simde__m128 rex_fused_madd(simde__m128 a, simde__m128 b, simde__m128 c) {\n"
    "  alignas(16) float x[4], y[4], z[4], r[4];\n"
    "  simde_mm_store_ps(x, a); simde_mm_store_ps(y, b); simde_mm_store_ps(z, c);\n"
    "  for (int i = 0; i < 4; ++i) r[i] = std::fmaf(x[i], y[i], z[i]);\n"
    "  return simde_mm_load_ps(r);\n"
    "}\n"
    "static inline simde__m128 rex_fused_nmsub(simde__m128 a, simde__m128 b, simde__m128 c) {\n"
    "  alignas(16) float x[4], y[4], z[4], r[4];\n"
    "  simde_mm_store_ps(x, a); simde_mm_store_ps(y, b); simde_mm_store_ps(z, c);\n"
    "  for (int i = 0; i < 4; ++i) r[i] = -std::fmaf(x[i], y[i], -z[i]);\n"
    "  return simde_mm_load_ps(r);\n"
    "}\n"
)

L = r"simde_mm_load_ps\(([\w.\[\]]+)\.f32\)"
MADD = re.compile(r"simde_mm_add_ps\(simde_mm_mul_ps\(" + L + r", " + L + r"\), " + L + r"\)")
NMSUB = re.compile(r"simde_mm_xor_ps\(simde_mm_sub_ps\(simde_mm_mul_ps\(" + L + r", " + L + r"\), " + L +
                   r"\), simde_mm_castsi128_ps\(simde_mm_set1_epi32\(int\(0x80000000\)\)\)\)")


def main():
    gen, mode, funcs = sys.argv[1], sys.argv[2], sys.argv[3:]
    files = sorted(f for f in os.listdir(gen) if f.startswith("renut_recomp.") and f.endswith(".cpp"))
    for name in files:
        path = os.path.join(gen, name)
        backup = path + ".before_fma"
        if mode == "revert":
            if os.path.exists(backup):
                os.replace(backup, path)
                print("restored", name)
            continue
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
        hit = [f for f in funcs if ("DEFINE_REX_FUNC(" + f + ")") in text]
        if not hit:
            continue
        lines = text.split("\n")
        out, inside, n_madd, n_nmsub = [], False, 0, 0
        for line in lines:
            if line.startswith("DEFINE_REX_FUNC("):
                inside = any(line.startswith("DEFINE_REX_FUNC(" + f + ")") for f in hit)
            if inside and "simde_mm_" in line:
                line, a = MADD.subn(r"rex_fused_madd(simde_mm_load_ps(\1.f32), simde_mm_load_ps(\2.f32), "
                                    r"simde_mm_load_ps(\3.f32))", line)
                line, b = NMSUB.subn(r"rex_fused_nmsub(simde_mm_load_ps(\1.f32), simde_mm_load_ps(\2.f32), "
                                     r"simde_mm_load_ps(\3.f32))", line)
                n_madd += a
                n_nmsub += b
            out.append(line)
        if not os.path.exists(backup):
            with open(backup, "w", encoding="utf-8") as fh:
                fh.write(text)
        new = "\n".join(out)
        # the helpers after the file's first include block
        at = new.find("\n", new.rfind("#include", 0, new.find("DEFINE_REX_FUNC(")))
        new = new[:at + 1] + HELPER + new[at + 1:]
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(new)
        print(name, ",".join(hit), "vmaddfp", n_madd, "vnmsubfp", n_nmsub)


if __name__ == "__main__":
    main()
