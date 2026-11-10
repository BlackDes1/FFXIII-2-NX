#!/usr/bin/env python3
"""ffxiii2-nx: the smoke flicker band (docs/diagnostics/BUGS.md D-003) counted in every frame of a Frame Tap recording.

  python tools/visual_debug/band_scan.py <recording.mp4 | folder of frames> [--split 336] [--share 0.37]
                                         [--out <dir for the banded frames>] [--report report.json]

A frame is banded when, at one row within 8 of the split, the sampled columns (x 40..1240, every 8 px) fall by more
than 10 grey levels from 2 rows above to 2 rows below in >= --share of them (the same edge test as
tools/switch_tests/flicker_metric.py --method edge; calibrated on the user's video 2026110513383000_s.mp4: banded frames
0.50-0.54, others <= 0.24). Every decoded frame is checked (a recording holds each game frame once: the viewer's
--headless --mp4 keeps the console's H.264 as sent). Needs numpy, PIL and, for a video, ffmpeg on PATH (the Linux VM).
Writes the banded frames (and their neighbours) as PNG to --out for reading next to the logs.
"""
import argparse
import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image


def frames_of(src, tmp):
    if os.path.isdir(src):
        fs = sorted(glob.glob(os.path.join(src, "*.png")) + glob.glob(os.path.join(src, "*.pgm")) +
                    glob.glob(os.path.join(src, "*.jpg")))
        return fs
    ff = shutil.which("ffmpeg")
    if not ff:
        raise SystemExit("ffmpeg is not on PATH: decode the recording where it is (the Linux VM)")
    subprocess.run([ff, "-v", "error", "-i", src, "-vsync", "0", "-vf", "format=gray",
                    os.path.join(tmp, "f%06d.pgm")], check=True, stderr=subprocess.DEVNULL)
    return sorted(glob.glob(os.path.join(tmp, "*.pgm")))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src")
    ap.add_argument("--split", type=int, default=336)
    ap.add_argument("--share", type=float, default=0.37)
    ap.add_argument("--out")
    ap.add_argument("--report")
    ap.add_argument("--stamp", action="store_true",
                    help="read the frame number ffxiii2_diag_frame_stamp put in the bottom-left corner (renderer frame)")
    a = ap.parse_args()
    tmp = tempfile.mkdtemp(prefix="band_scan_")
    try:
        fs = frames_of(a.src, tmp)
        if not fs:
            print("no frames")
            return 1
        res = []
        for i, f in enumerate(fs):
            g = np.asarray(Image.open(f).convert("L"), dtype=np.int16)
            h, w = g.shape
            s = int(round(a.split * h / 720.0))
            cols = np.arange(40, w - 40, 8)
            best = (0.0, s)
            for y in range(max(2, s - 8), min(h - 3, s + 9)):
                share = float(np.mean((g[y + 2, cols] - g[y - 2, cols]) < -10))
                best = max(best, (share, y))
            r = {"frame": i + 1, "share": round(best[0], 3), "row": best[1]}
            if a.stamp:
                # ffxiii2_diag_frame_stamp: white marker at x 0-7, black at 8-15, then 20 bits of 8 px (LSB first)
                # in the bottom 6 rows; read at each square's centre (2 rows above the bottom, H.264 smears edges)
                yy = h - 3
                ok = g[yy, 4] > 160 and g[yy, 12] < 90
                n = 0
                for bit in range(20):
                    if g[yy, 16 + bit * 8 + 4] > 128:
                        n |= 1 << bit
                r["stamp"] = n if ok else None
            res.append(r)
        banded = [r for r in res if r["share"] >= a.share]
        print(f"{len(res)} frames, banded {len(banded)} ({100.0 * len(banded) / len(res):.2f} %)"
              + (f": frames {[r['frame'] for r in banded][:40]}" if banded else ""))
        if a.stamp:
            got = [r for r in res if r.get("stamp") is not None]
            print(f"stamps read in {len(got)} of {len(res)} frames; banded renderer frames: "
                  f"{[r.get('stamp') for r in banded][:60]}")
        shares = sorted(r["share"] for r in res)
        print(f"edge share: median {shares[len(shares) // 2]:.2f}, 99th {shares[int(len(shares) * 0.99)]:.2f}, "
              f"max {shares[-1]:.2f}")
        if a.out and banded:
            os.makedirs(a.out, exist_ok=True)
            want = {r["frame"] + d for r in banded for d in (-1, 0, 1)}
            for n in sorted(want):
                if 1 <= n <= len(fs):
                    Image.open(fs[n - 1]).save(os.path.join(a.out, f"frame_{n:06d}.png"))
        if a.report:
            json.dump({"src": a.src, "frames": len(res), "banded": banded, "share_threshold": a.share,
                       "per_frame": res}, open(a.report, "w"), indent=0)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
