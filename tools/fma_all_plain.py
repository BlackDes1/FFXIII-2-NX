#!/usr/bin/env python3
"""ffxiii2-nx k237: every rex::ppc::rex_vmaddfp / rex_vnmsubfp in the generated code (renut_recomp.*.cpp, hot/*.h) back
to the multiply-then-add text the codegen emitted before k236 (the fused vmaddfp did not fix D-009; reverted).
usage: fma_all_plain.py <generated dir>"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fma_forms as F  # noqa: E402


def main():
    gen = sys.argv[1]
    files = [os.path.join(gen, n) for n in os.listdir(gen) if n.startswith("renut_recomp.") and n.endswith(".cpp")]
    files += [os.path.join(gen, "hot", n) for n in os.listdir(os.path.join(gen, "hot")) if n.endswith(".h")]
    changed = 0
    for path in sorted(files):
        with open(path, encoding="utf-8", newline="") as fh:
            text = fh.read()
        new = F.FUSED_NMSUB.sub(F.TO_PLAIN_NMSUB, F.FUSED_MADD.sub(F.TO_PLAIN_MADD, text))
        if new != text:
            with open(path, "w", encoding="utf-8", newline="") as fh:
                fh.write(new)
            changed += 1
    print(changed, "files back to multiply-then-add")


if __name__ == "__main__":
    main()
