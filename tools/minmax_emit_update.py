#!/usr/bin/env python3
"""ffxiii2-nx k237 (D-009): rewrite already generated code to what the updated codegen emits for vmaxfp / vminfp (and
vmaxfp128 / vminfp128): rex::ppc::rex_vmaxfp / rex_vminfp (the VMX NaN and signed-zero results, rex/ppc/intrinsics.h)
instead of simde_mm_max_ps / simde_mm_min_ps. Only whole statements of the codegen's form
    simde_mm_store_ps(D.f32, simde_mm_max_ps(simde_mm_load_ps(A.f32), simde_mm_load_ps(B.f32)));
are touched (the same text a fresh codegen gives); in renut_recomp.*.cpp the statement must follow a vmaxfp / vminfp
comment (checked: counts printed per form), in the hot copies (no comments) the statement form alone decides.

usage: minmax_emit_update.py <generated dir>
"""
import os
import re
import sys

L = r"simde_mm_load_ps\(([\w.\[\]]+)\.f32\)"
STMT = re.compile(r"simde_mm_store_ps\(([\w.\[\]]+)\.f32, simde_mm_(max|min)_ps\(" + L + r", " + L + r"\)\);")
COMMENT = re.compile(r"^\s*// (vmaxfp|vmaxfp128|vminfp|vminfp128) ")


def rewrite(m):
    return (f"simde_mm_store_ps({m.group(1)}.f32, rex::ppc::rex_v{m.group(2)}fp(simde_mm_load_ps({m.group(3)}.f32), "
            f"simde_mm_load_ps({m.group(4)}.f32)));")


def main():
    gen = sys.argv[1]
    stats = {"commented": 0, "uncommented_generated": 0, "hot": 0, "comments_without_stmt": 0}
    for name in sorted(os.listdir(gen)):
        if not (name.startswith("renut_recomp.") and name.endswith(".cpp")):
            continue
        path = os.path.join(gen, name)
        with open(path, encoding="utf-8", newline="") as fh:
            lines = fh.read().split("\n")
        changed = False
        pending = False
        for i, line in enumerate(lines):
            if COMMENT.match(line):
                if pending:
                    stats["comments_without_stmt"] += 1
                pending = True
                continue
            m = STMT.search(line)
            if m:
                if pending:
                    stats["commented"] += 1
                else:
                    stats["uncommented_generated"] += 1
                lines[i] = STMT.sub(rewrite, line)
                changed = True
                pending = False
            elif pending and line.strip() and not line.lstrip().startswith("//") and "simde_mm_store_ps" in line:
                stats["comments_without_stmt"] += 1
                pending = False
        if changed:
            with open(path, "w", encoding="utf-8", newline="") as fh:
                fh.write("\n".join(lines))
    hot = os.path.join(gen, "hot")
    for name in sorted(os.listdir(hot)):
        if not name.endswith(".h"):
            continue
        path = os.path.join(hot, name)
        with open(path, encoding="utf-8", newline="") as fh:
            text = fh.read()
        new, n = STMT.subn(rewrite, text)
        if n:
            stats["hot"] += n
            with open(path, "w", encoding="utf-8", newline="") as fh:
                fh.write(new)
    print(stats)


if __name__ == "__main__":
    main()
