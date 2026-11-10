#!/usr/bin/env python3
"""ffxiii2-nx k237 test (D-009): choose per instruction form whether the generated vmaddfp / vnmsubfp is fused.

The statement after a "// vmaddfp ...", "// vmaddfp128 ...", "// vmaddcfp128 ...", "// vnmsubfp ..." or
"// vnmsubfp128 ..." comment is rewritten to the fused helper (rex::ppc::rex_vmaddfp / rex_vnmsubfp) or to the old
multiply-then-add text.

usage: fma_forms.py <generated dir> <fused forms, comma separated, or "none">
  e.g. fma_forms.py app/generated vmaddfp,vnmsubfp     (VMX forms fused, the VMX128 forms not)
Only renut_recomp.*.cpp (hot copies have no comments: they keep what fma_emit_update.py gave them).
"""
import os
import re
import sys

FORMS = ("vmaddfp", "vmaddfp128", "vmaddcfp128", "vnmsubfp", "vnmsubfp128")
L = r"simde_mm_load_ps\(([\w.\[\]]+)\.f32\)"
FUSED_MADD = re.compile(r"rex::ppc::rex_vmaddfp\(" + L + r", " + L + r", " + L + r"\)")
FUSED_NMSUB = re.compile(r"rex::ppc::rex_vnmsubfp\(" + L + r", " + L + r", " + L + r"\)")
PLAIN_MADD = re.compile(r"simde_mm_add_ps\(simde_mm_mul_ps\(" + L + r", " + L + r"\), " + L + r"\)")
PLAIN_NMSUB = re.compile(r"simde_mm_xor_ps\(simde_mm_sub_ps\(simde_mm_mul_ps\(" + L + r", " + L + r"\), " + L +
                         r"\), simde_mm_castsi128_ps\(simde_mm_set1_epi32\(int\(0x80000000\)\)\)\)")
TO_FUSED_MADD = r"rex::ppc::rex_vmaddfp(simde_mm_load_ps(\1.f32), simde_mm_load_ps(\2.f32), simde_mm_load_ps(\3.f32))"
TO_FUSED_NMSUB = r"rex::ppc::rex_vnmsubfp(simde_mm_load_ps(\1.f32), simde_mm_load_ps(\2.f32), simde_mm_load_ps(\3.f32))"
TO_PLAIN_MADD = (r"simde_mm_add_ps(simde_mm_mul_ps(simde_mm_load_ps(\1.f32), simde_mm_load_ps(\2.f32)), "
                 r"simde_mm_load_ps(\3.f32))")
TO_PLAIN_NMSUB = (r"simde_mm_xor_ps(simde_mm_sub_ps(simde_mm_mul_ps(simde_mm_load_ps(\1.f32), simde_mm_load_ps(\2.f32)), "
                  r"simde_mm_load_ps(\3.f32)), simde_mm_castsi128_ps(simde_mm_set1_epi32(int(0x80000000))))")
COMMENT = re.compile(r"^\s*// (vmaddfp|vmaddfp128|vmaddcfp128|vnmsubfp|vnmsubfp128) ")


def main():
    gen, which = sys.argv[1], sys.argv[2]
    fused = set() if which == "none" else set(which.split(","))
    # optional: a file of function names (sub_XXXXXXXX), or lo-hi address ranges (8298C000-8298DFFF), one a line: only
    # those functions get the fused forms, all others the plain text.
    only_funcs, only_ranges = None, []
    if len(sys.argv) > 3:
        only_funcs = set()
        for item in open(sys.argv[3]).read().split():
            if "-" in item:
                lo, hi = item.split("-")
                only_ranges.append((int(lo, 16), int(hi, 16)))
            else:
                only_funcs.add(item)
    current_ok = True
    counts = {f: [0, 0] for f in FORMS}
    for name in sorted(os.listdir(gen)):
        if not (name.startswith("renut_recomp.") and name.endswith(".cpp")):
            continue
        path = os.path.join(gen, name)
        with open(path, encoding="utf-8", newline="") as fh:
            lines = fh.read().split("\n")
        changed = False
        form = None
        for i, line in enumerate(lines):
            if line.startswith("DEFINE_REX_FUNC(sub_"):
                fn = line[len("DEFINE_REX_FUNC("):line.index(")")]
                if only_funcs is None:
                    current_ok = True
                else:
                    addr = int(fn[4:], 16)
                    current_ok = fn in only_funcs or any(lo <= addr <= hi for lo, hi in only_ranges)
            m = COMMENT.match(line)
            if m:
                form = m.group(1)
                continue
            if form is None or "simde_mm_store_ps" not in line:
                if line.strip() and not line.lstrip().startswith("//") and form is not None and "fpscr" not in line:
                    form = None
                continue
            want = form in fused and current_ok
            if form.startswith("vnmsub"):
                new = FUSED_NMSUB.sub(TO_PLAIN_NMSUB, line) if not want else PLAIN_NMSUB.sub(TO_FUSED_NMSUB, line)
            else:
                new = FUSED_MADD.sub(TO_PLAIN_MADD, line) if not want else PLAIN_MADD.sub(TO_FUSED_MADD, line)
            counts[form][0 if want else 1] += 1
            if new != line:
                lines[i] = new
                changed = True
            form = None
        if changed:
            with open(path, "w", encoding="utf-8", newline="") as fh:
                fh.write("\n".join(lines))
    for f in FORMS:
        print(f"{f}: fused {counts[f][0]}, plain {counts[f][1]}")


if __name__ == "__main__":
    main()
