#!/usr/bin/env python3
"""ffxiii2-nx (2026-11-05): how often a series of console screenshots shows the flicker bands.

  python tools/switch_tests/flicker_metric.py <folder of screenshots> [--split 336]

The flicker (video 2026110513383000_s.mp4, Chaos Bahamut battle): in bad frames the smoke layer is missing above
y ~ 336 (the first predicated tile of a 1280x720 64-bit target at 2x MSAA), and the brighter sky shows there. Per
screenshot this measures the mean luminance of the band just above the split (x 100-1180, y split-60..split-4) and
just below it (y split+4..split+60); a frame is "banded" when the band above is brighter than the one below by
more than --jump (default 5 on 0-255; the user's video: bad frames +9.5, good ones -1.5..+0.6) while the scene's own gradient (median over all frames) is subtracted. Prints
one line per frame and the share of banded frames: the number to compare between two configs (same scene).
"""
import argparse
import glob
import os
import statistics
import sys

from PIL import Image, ImageStat


def bands(path, split):
    im = Image.open(path).convert("L")
    w, h = im.size
    s = split * h / 720.0
    sx = w / 1280.0
    above = im.crop((int(100 * sx), int(s - 60 * h / 720), int(1180 * sx), int(s - 4 * h / 720)))
    below = im.crop((int(100 * sx), int(s + 4 * h / 720), int(1180 * sx), int(s + 60 * h / 720)))
    return ImageStat.Stat(above).mean[0], ImageStat.Stat(below).mean[0]


def edge(path, split, window=8, drop=10):
    """ffxiii2-nx (2026-11-06): the band's own signature in the user's video (2026110513383000_s.mp4, 10 fps: 9 of 97
    frames): a horizontal edge at y 334-337 across most of the width, the picture darker just below it. Per row near
    the split: how many of the sampled columns (x 40..w-40, every 8 px) fall by more than `drop` from 2 rows above to 2
    rows below. Returns (best share of columns, its row). The user's banded frames: 0.50-0.54; the others <= 0.24.
    The old above/below mean (bands()) flagged 20-30 % of frames with no edge at all (scene gradient)."""
    im = Image.open(path).convert("L")
    w, h = im.size
    px = im.load()
    s = int(round(split * h / 720.0))
    cols = range(40, w - 40, 8)
    best = (0.0, s)
    for y in range(max(2, s - window), min(h - 3, s + window + 1)):
        n = sum(1 for x in cols if px[x, y + 2] - px[x, y - 2] < -drop)
        best = max(best, (n / len(cols), y))
    return best


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folder")
    ap.add_argument("--split", type=float, default=336)
    ap.add_argument("--jump", type=float, default=5)
    ap.add_argument("--glob", default="*.jpg")
    ap.add_argument("--method", choices=["edge", "mean"], default="edge",
                    help="edge (default since 2026-11-06): the horizontal edge at the split; mean: the old above/below "
                         "brightness difference (false positives)")
    ap.add_argument("--edge-share", type=float, default=0.37,
                    help="edge: share of columns that must fall at one row (user's banded frames 0.50-0.54, others <= 0.24)")
    ap.add_argument("--all-screens", action="store_true",
                    help="count every shot; by default only the battle screens (nav_ffxiii2's state: HUD up, no dialog), "
                         "since cutscenes and the tutorial banner (a line at y 326) gave false bands (2026-11-05, test G)")
    a = ap.parse_args()
    files = sorted(glob.glob(os.path.join(a.folder, a.glob)))
    if not a.all_screens:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import nav_ffxiii2  # noqa: E402
        kept = []
        for f in files:
            with open(f, "rb") as fh:
                st = nav_ffxiii2.state(nav_ffxiii2.features(fh.read()))
                # the edge method also reads frames with the tutorial dialog up (the band shows beside it)
                if st == "battle" or (a.method == "edge" and st == "battle_dialog"):
                    kept.append(f)
        print(f"battle screens: {len(kept)} of {len(files)}")
        files = kept
    if not files:
        print("no screenshots")
        return 1
    if a.method == "edge":
        banded = 0
        for f in files:
            share, row = edge(f, a.split)
            flag = share >= a.edge_share
            banded += flag
            print(f"{os.path.basename(f):28s} edge {share:5.2f} at y {row:3d} {'BANDED' if flag else ''}")
        print(f"banded frames: {banded} of {len(files)} ({100.0 * banded / len(files):.0f}%), edge method")
        return 0
    vals = [(f, *bands(f, a.split)) for f in files]
    base = statistics.median(up - lo for _, up, lo in vals)
    banded = 0
    for f, up, lo in vals:
        d = (up - lo) - base
        flag = d > a.jump
        banded += flag
        print(f"{os.path.basename(f):28s} above {up:6.1f} below {lo:6.1f} step {d:+6.1f} {'BANDED' if flag else ''}")
    print(f"banded frames: {banded} of {len(vals)} ({100.0 * banded / len(vals):.0f}%), scene step {base:+.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
