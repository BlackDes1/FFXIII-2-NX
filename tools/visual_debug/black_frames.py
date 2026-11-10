#!/usr/bin/env python3
"""ffxiii2-nx (2026-11-07, k239): black drop-out frames in a recording (the Switch opening: the scene black for single
frames or runs of frames while the shot goes on).

  python3 black_frames.py <video.mp4> [--from S] [--to S] [--black 3] [--scene 12]

Decodes every frame (ffmpeg, grey 160x90), mean brightness per frame. A drop-out is a frame of mean <= --black that
lies between frames of mean >= --scene within --gap frames on both sides (a cut to black that lasts longer, a fade,
is not counted). Prints the totals and the drop-out runs with their frame numbers and times.
"""
import argparse
import subprocess

import numpy as np


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('video')
    ap.add_argument('--from', dest='t0', type=float, default=0.0)
    ap.add_argument('--to', dest='t1', type=float, default=0.0)
    ap.add_argument('--black', type=float, default=3.0)
    ap.add_argument('--scene', type=float, default=12.0)
    ap.add_argument('--gap', type=int, default=12)
    a = ap.parse_args()
    cmd = ['ffmpeg', '-loglevel', 'error']
    if a.t0:
        cmd += ['-ss', str(a.t0)]
    cmd += ['-i', a.video]
    if a.t1:
        cmd += ['-t', str(a.t1 - a.t0)]
    cmd += ['-vsync', '0', '-vf', 'scale=160:90,format=gray', '-f', 'rawvideo', '-']
    raw = subprocess.run(cmd, capture_output=True, check=True).stdout
    fr = np.frombuffer(raw, np.uint8).reshape(-1, 90, 160)
    m = fr.reshape(len(fr), -1).mean(axis=1)
    n = len(m)
    black = m <= a.black
    drop = np.zeros(n, bool)
    for i in np.where(black)[0]:
        lo = [j for j in range(max(0, i - a.gap), i) if m[j] >= a.scene]
        hi = [j for j in range(i + 1, min(n, i + a.gap + 1)) if m[j] >= a.scene]
        drop[i] = bool(lo and hi)
    scene = m >= a.scene
    print(f'frames {n}: scene {int(scene.sum())}, black {int(black.sum())}, drop-outs {int(drop.sum())} '
          f'({100.0 * drop.sum() / max(1, scene.sum() + drop.sum()):.1f} % of the shot frames)')
    runs, i = [], 0
    while i < n:
        if drop[i]:
            j = i
            while j + 1 < n and drop[j + 1]:
                j += 1
            runs.append((i, j))
            i = j + 1
        else:
            i += 1
    print('runs (first-last frame, length):', ' '.join(f'{s}-{e}({e - s + 1})' for s, e in runs[:80]))


if __name__ == '__main__':
    main()
