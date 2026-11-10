#!/usr/bin/env python3
"""D-010 helper: correlate stale (oddball) captured frames with the presented front buffer.

Inputs: a banjo_video rgba file (+ its .txt: w h fps n), renut logs containing
'[capframe] capture N = renderer frame F' and '[rtlog] frame F ...' lines.
Pure Python (no numpy needed). Read-only; writes only --out.
oddball(i) = min(d(i-1,i), d(i,i+1)) - d(i-1,i+1): large when frame i differs from both
neighbours while the neighbours agree (a single stale/foreign frame).
"""
import argparse, re, sys

CAP = re.compile(r'\[capframe\] capture (\d+) = renderer frame (\d+)')
RT = re.compile(r'\[rtlog\] frame (\d+)(.*)')
SW = re.compile(r'S([0-9A-Fa-f]{8})/age(\d+)/([0-9A-Fa-f]+)')
RS = re.compile(r'R([0-9A-Fa-f]{8})>([0-9A-Fa-f]{8})')


def thumbs(path, w, h, n, step=8):
    fr = w * h * 4
    out = []
    with open(path, 'rb') as f:
        for _ in range(n):
            b = f.read(fr)
            if len(b) < fr:
                break
            t = []
            for y in range(0, h, step):
                row = y * w * 4
                for x in range(0, w, step):
                    o = row + x * 4
                    t.append((b[o] * 3 + b[o + 1] * 6 + b[o + 2]) // 10)
            out.append(t)
    return out


def dist(a, b):
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('video')
    ap.add_argument('logs', nargs='+')
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    with open(a.video[:-5] + '.txt') as f:
        w, h, fps, n = [int(float(x)) for x in f.read().split()[:4]]
    th = thumbs(a.video, w, h, n)
    cap2f, rt, rtbody = {}, {}, {}
    for lp in a.logs:
        with open(lp, encoding='utf-8', errors='replace') as f:
            for line in f:
                m = CAP.search(line)
                if m:
                    cap2f[int(m.group(1))] = int(m.group(2))
                    continue
                m = RT.search(line)
                if m:
                    fr = int(m.group(1)); body = m.group(2)
                    s = SW.search(body)
                    rt[fr] = (s.groups() if s else None, len(RS.findall(body)), body.count(' D'), len(body))
                    rtbody[fr] = body.strip()
    out = ['captures %d (%dx%d), capframe lines %d, rtlog frames %d' % (len(th), w, h, len(cap2f), len(rt))]
    out.append('cap  rframe  odd   dprev  dnext  front     age host      resolves druns')
    stats = {}
    caps = sorted(cap2f)
    # captures are numbered from 0 or 1; map index into thumbs by order
    for k, c in enumerate(caps):
        i = k
        if i <= 0 or i >= len(th) - 1:
            continue
        dp, dn, dpn = dist(th[i - 1], th[i]), dist(th[i], th[i + 1]), dist(th[i - 1], th[i + 1])
        odd = min(dp, dn) - dpn
        f = cap2f[c]
        r = rt.get(f)
        sw = r[0] if r and r[0] else ('?', '?', '?')
        out.append('%4d %7d %6.1f %6.1f %6.1f  %s %3s %-9s %3s %3s' % (
            c, f, odd, dp, dn, sw[0], sw[1], sw[2], r[1] if r else '?', r[2] if r else '?'))
        key = (sw[0], sw[2])
        st = stats.setdefault(key, [0, 0])
        st[0] += 1
        if odd > 8:
            st[1] += 1
    # brightness classification of the 3D area (rows 15%..80%: excludes the subtitle band)
    def scene_mean(t):
        cols = (w + 7) // 8
        rows = len(t) // cols
        r0, r1 = int(rows * 0.15), int(rows * 0.80)
        seg = t[r0 * cols:r1 * cols]
        return sum(seg) / max(1, len(seg))
    out.append('')
    out.append('cap rframe scene_mean class  front     resolves druns n2D0 bodylen')
    black, good = [], []
    for k, c in enumerate(caps):
        if k >= len(th):
            break
        m = scene_mean(th[k]); f = cap2f[c]; r = rt.get(f)
        cls = 'BLACK' if m < 6 else 'ok'
        n2d0 = rtbody.get(f, '').count('6CB07E60')
        out.append('%4d %6d %7.1f %-6s %s %3s %3s %3d %6s' % (c, f, m, cls, (r[0][0] if r and r[0] else '?'), r[1] if r else '?', r[2] if r else '?', n2d0, r[3] if r else '?'))
        (black if cls == 'BLACK' else good).append(f)
    out.append('black %d, ok %d' % (len(black), len(good)))
    def avg(lst, idx):
        v = [rt[f][idx] for f in lst if f in rt]
        return sum(v) / max(1, len(v))
    out.append('avg resolves black %.1f ok %.1f; avg druns black %.1f ok %.1f; avg bodylen black %.0f ok %.0f' % (avg(black, 1), avg(good, 1), avg(black, 2), avg(good, 2), avg(black, 3), avg(good, 3)))
    # full rtlog bodies of the first black frame after an ok frame, and that ok frame
    for k in range(1, len(caps)):
        fb, fp = cap2f[caps[k]], cap2f[caps[k - 1]]
        if fb in black and fp in good and fb in rtbody and fp in rtbody:
            out.append('')
            out.append('OK frame %d body:' % fp)
            out.append(rtbody[fp])
            out.append('BLACK frame %d body:' % fb)
            out.append(rtbody[fb])
            break
    out.append('')
    out.append('front buffer / host texture: captures, oddball(>8) count')
    for k, v in sorted(stats.items(), key=lambda kv: -kv[1][0]):
        out.append('  %s %s: %d, %d' % (k[0], k[1], v[0], v[1]))
    with open(a.out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(out) + '\n')
    print('\n'.join(out[-12:]))


if __name__ == '__main__':
    sys.exit(main())
