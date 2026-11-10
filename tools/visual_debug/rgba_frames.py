#!/usr/bin/env python3
"""ffxiii2-nx: the PC's raw short video (banjo_video_s: capturas/video_<s>s.rgba, R8G8B8X8 frames back to back, with a
.txt beside it giving width and height) split into one grey PNG per frame, for tools/visual_debug/band_scan.py.

  python tools/visual_debug/rgba_frames.py <video_NNNs.rgba> <out dir> [--width 1280 --height 720] [--color]

The size is read from the .txt when it has "width" / "height" numbers; otherwise --width / --height. Read-only on the
input. Needs numpy and PIL.
"""
import argparse
import os
import re
import sys

import numpy as np
from PIL import Image


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src")
    ap.add_argument("out")
    ap.add_argument("--width", type=int, default=0)
    ap.add_argument("--height", type=int, default=0)
    ap.add_argument("--color", action="store_true", help="write RGB PNGs instead of grey")
    a = ap.parse_args()
    w, h = a.width, a.height
    txt = os.path.splitext(a.src)[0] + ".txt"
    if (not w or not h) and os.path.exists(txt):
        t = open(txt, errors="replace").read()
        nums = [int(x) for x in re.findall(r"\d+", t)]
        mw = re.search(r"width\D*(\d+)", t, re.I)
        mh = re.search(r"height\D*(\d+)", t, re.I)
        if mw and mh:
            w, h = int(mw.group(1)), int(mh.group(1))
        elif len(nums) >= 2:
            w, h = nums[0], nums[1]
    if not w or not h:
        w, h = 1280, 720
    frame = w * h * 4
    size = os.path.getsize(a.src)
    n = size // frame
    print(f"{a.src}: {w}x{h}, {n} frames ({size} bytes)")
    os.makedirs(a.out, exist_ok=True)
    with open(a.src, "rb") as f:
        for i in range(n):
            buf = np.frombuffer(f.read(frame), dtype=np.uint8).reshape(h, w, 4)
            img = Image.fromarray(buf[:, :, :3].copy(), "RGB")
            if not a.color:
                img = img.convert("L")
            img.save(os.path.join(a.out, f"frame_{i + 1:06d}.png"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
