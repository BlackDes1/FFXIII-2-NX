#!/usr/bin/env python3
"""ffxiii2-nx k237 test: the hot copies (app/generated/hot/*.h) fused or plain for vmaddfp / vnmsubfp.
usage: fma_hot.py <generated/hot dir> <fused|plain>"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fma_forms as F  # noqa: E402


def main():
    hot, mode = sys.argv[1], sys.argv[2]
    n = 0
    for name in sorted(os.listdir(hot)):
        if not name.endswith(".h"):
            continue
        path = os.path.join(hot, name)
        with open(path, encoding="utf-8", newline="") as fh:
            text = fh.read()
        if mode == "plain":
            new = F.FUSED_NMSUB.sub(F.TO_PLAIN_NMSUB, F.FUSED_MADD.sub(F.TO_PLAIN_MADD, text))
        else:
            new = F.PLAIN_NMSUB.sub(F.TO_FUSED_NMSUB, F.PLAIN_MADD.sub(F.TO_FUSED_MADD, text))
        if new != text:
            with open(path, "w", encoding="utf-8", newline="") as fh:
                fh.write(new)
            n += 1
    print(mode, n, "hot files changed")


if __name__ == "__main__":
    main()
