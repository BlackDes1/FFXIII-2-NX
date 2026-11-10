#!/usr/bin/env python3
"""ffxiii2-nx visual debugging with Switch Frame Tap: sessions, screenshots, recordings, comparisons.

Everything a test session makes lives under  logs/visual/<build_id>/<test_session>/:
    session.json      build id, toml, mode (visual | perf), start time, notes, per-shot metadata
    shots/            native-resolution PNGs (Frame Tap's combo, fetched over sys-botbase/FTP); 000_<label>.png
    recordings/       MP4s the PC viewer recorded (R key) during the session
    frames/           frames cut out of a recording (ffmpeg)
    diffs/            comparison output: heatmaps, side-by-side pictures, compare.json

  python tools/visual_debug/vd.py new <build_id> <test_session> [--toml X] [--mode visual|perf] [--note ...]
  python tools/visual_debug/vd.py shoot <session_dir> [--count N] [--every S] [--label L] [--combo ZL,ZR,DDOWN]
        presses the Frame Tap screenshot combo through sys-botbase (held 150 ms; Frame Tap polls every 25 ms),
        waits for the PNG to appear on the SD card, copies it to shots/ and records the time and nav label
  python tools/visual_debug/vd.py fetch <session_dir>            copy every new Frame Tap PNG into shots/
  python tools/visual_debug/vd.py recordings <session_dir> [--src DIR]
        copies MP4s newer than the session start from the viewer's folder (default %USERPROFILE%\\Videos\\Switch Frame Tap)
  python tools/visual_debug/vd.py frames <video> <out_dir> [--every S | --at t1,t2,...]   (needs ffmpeg on PATH)
  python tools/visual_debug/vd.py compare <a.png> <b.png> <out_dir> [--threshold T] [--rows y0:y1]
        per-channel mean / max difference, share of pixels over T, the changed bounding box, the row band with the
        most change, a heatmap (black = same, white = 64+ levels apart) and an A | B | heatmap picture
  python tools/visual_debug/vd.py builds <session_a> <session_b> [--threshold T]
        pairs shots by label (else by index) between two sessions (two builds or two configs) and compares each pair;
        writes <session_b>/diffs/vs_<a>/compare.json and an index.html
  python tools/visual_debug/vd.py index <session_dir>             contact sheet index.html of shots/ and diffs/

Modes: a "visual" session runs with Frame Tap enabled (screenshots, optionally a stream); a "perf" session must have
Frame Tap's sysmodule off (manager: Start with the console = off, reboot) because its display/binder interception and
VIC copies cost console time. `new --mode perf` records that requirement; benchmark numbers from a visual session are
not comparable and are marked so in session.json.
"""
import argparse
import datetime
import glob
import html
import json
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.dirname(HERE))

SHOTS_SD = "/switch/switch-frame-tap/screenshots"


def now_iso():
    return datetime.datetime.now().isoformat(timespec="seconds")


def load_session(d):
    p = os.path.join(d, "session.json")
    return json.load(open(p)) if os.path.exists(p) else {"shots": []}


def save_session(d, s):
    json.dump(s, open(os.path.join(d, "session.json"), "w"), indent=1)


def cmd_new(a):
    d = os.path.join(REPO, "logs", "visual", a.build_id, a.test_session)
    for sub in ("shots", "recordings", "frames", "diffs"):
        os.makedirs(os.path.join(d, sub), exist_ok=True)
    s = load_session(d)
    s.update({"build_id": a.build_id, "test_session": a.test_session, "toml": a.toml, "mode": a.mode,
              "started": s.get("started") or now_iso(), "started_epoch": s.get("started_epoch") or time.time(),
              "notes": (s.get("notes") or []) + ([a.note] if a.note else []),
              "benchmark_valid": a.mode == "perf",
              "frame_tap_required": "on" if a.mode == "visual" else "off (sysmodule not started)"})
    s.setdefault("shots", [])
    save_session(d, s)
    print(d)
    return 0


def _sd(cfg_args):
    from switch_remote import sdfiles, switchcfg
    cfg = switchcfg.from_args(cfg_args)
    return cfg, sdfiles.open_sd(cfg, cfg_args.transport)


def _list_pngs(sd):
    try:
        return sorted(e["name"] for e in sd.list(SHOTS_SD) if e["type"] != "dir" and e["name"].lower().endswith(".png"))
    except Exception:  # noqa: BLE001
        return []


def _copy_new(sd, session_dir, s, seen, label, t_press=None):
    got = []
    for n in _list_pngs(sd):
        if n in seen:
            continue
        idx = len(s["shots"])
        dst_name = f"{idx:03d}_{label or 'shot'}.png"
        # Frame Tap may still be writing the PNG: the read fails (-1) or returns a file without its IEND chunk.
        # Leave it for the next poll then.
        try:
            data = sd.read(SHOTS_SD + "/" + n)
        except Exception:  # noqa: BLE001
            continue
        if not data.endswith(b"IEND\xaeB`\x82"):
            continue
        open(os.path.join(session_dir, "shots", dst_name), "wb").write(data)
        s["shots"].append({"file": dst_name, "sd_name": n, "label": label, "pressed": t_press, "fetched": now_iso(),
                           "bytes": len(data)})
        seen.add(n)
        got.append(dst_name)
    return got


def cmd_shoot(a):
    from switch_remote import botbase
    cfg, opener = _sd(a)
    s = load_session(a.session)
    combo = [c.strip() for c in a.combo.split(",") if c.strip()]
    with opener as sd:
        seen = set(_list_pngs(sd)) | {x["sd_name"] for x in s["shots"]}
        for i in range(a.count):
            t = now_iso()
            with botbase.connect(cfg) as b:
                for c in combo:
                    b.press(c)
                time.sleep(0.15)
                for c in combo:
                    b.release(c)
            got = []
            deadline = time.time() + a.wait
            while not got and time.time() < deadline:
                time.sleep(0.4)
                got = _copy_new(sd, a.session, s, seen, a.label, t)
            if a.also_botbase:
                # the system's own capture (sys-botbase pixelPeek, JPEG) right after, to tell a capture artefact
                # from the game's picture
                with botbase.connect(cfg) as b:
                    jpg = b.screenshot()
                jn = (got[0][:-4] if got else f"{len(s['shots']):03d}_{a.label or 'shot'}_nopng") + "_botbase.jpg"
                open(os.path.join(a.session, "shots", jn), "wb").write(jpg)
                s.setdefault("botbase_shots", []).append({"file": jn, "taken": now_iso(), "pairs": got[0] if got else None})
            print(f"[{i + 1}/{a.count}] {t}: " + (", ".join(got) if got else f"no PNG within {a.wait:.0f} s (combo, setting or game not captured? read config/switch-frame-tap/log.txt)"))
            save_session(a.session, s)
            if i + 1 < a.count:
                time.sleep(a.every)
    return 0


def cmd_fetch(a):
    _, opener = _sd(a)
    s = load_session(a.session)
    with opener as sd:
        got = _copy_new(sd, a.session, s, {x["sd_name"] for x in s["shots"]}, a.label)
    save_session(a.session, s)
    print(f"{len(got)} new screenshots")
    return 0


def cmd_recordings(a):
    s = load_session(a.session)
    src = a.src or os.path.join(os.path.expanduser("~"), "Videos", "Switch Frame Tap")
    t0 = s.get("started_epoch", 0)
    n = 0
    for f in sorted(glob.glob(os.path.join(src, "*.mp4"))):
        if os.path.getmtime(f) < t0:
            continue
        dst = os.path.join(a.session, "recordings", os.path.basename(f))
        if not os.path.exists(dst):
            shutil.copy2(f, dst)
            n += 1
            print(os.path.basename(f))
    print(f"{n} recordings copied from {src}")
    return 0


def cmd_frames(a):
    os.makedirs(a.out_dir, exist_ok=True)
    ff = shutil.which("ffmpeg")
    if not ff:
        print("ffmpeg is not on PATH here; run this step where it is (the Linux VM has it)")
        return 2
    if a.at:
        for t in a.at.split(","):
            out = os.path.join(a.out_dir, f"t{float(t):08.3f}.png")
            subprocess.run([ff, "-v", "error", "-y", "-ss", t, "-i", a.video, "-frames:v", "1", out], check=True)
    else:
        subprocess.run([ff, "-v", "error", "-y", "-i", a.video, "-vf", f"fps=1/{a.every}",
                        os.path.join(a.out_dir, "f%05d.png")], check=True)
    print(len(os.listdir(a.out_dir)), "frames in", a.out_dir)
    return 0


def compare(pa, pb, out_dir, threshold=16, rows=None, tag=None):
    from PIL import Image, ImageChops, ImageOps
    A = Image.open(pa).convert("RGB")
    B = Image.open(pb).convert("RGB")
    note = None
    if A.size != B.size:
        note = f"sizes differ {A.size} vs {B.size}: B scaled to A"
        B = B.resize(A.size, Image.BILINEAR)
    if rows:
        y0, y1 = (int(v) for v in rows.split(":"))
        A, B = A.crop((0, y0, A.width, y1)), B.crop((0, y0, B.width, y1))
    D = ImageChops.difference(A, B)
    G = D.convert("L")
    hist = G.histogram()
    total = A.width * A.height
    over = sum(hist[threshold:])
    stat = {"a": pa, "b": pb, "size": list(A.size), "note": note,
            "mean_abs": [round(sum(i * c for i, c in enumerate(D.getchannel(k).histogram())) / total, 3) for k in range(3)],
            "max_abs": max(max(i for i, c in enumerate(D.getchannel(k).histogram()) if c) for k in range(3)),
            "over_threshold": threshold, "pixels_over": over, "share_over": round(over / total, 6),
            "bbox_over": None, "worst_band": None}
    mask = G.point(lambda v: 255 if v >= threshold else 0)
    stat["bbox_over"] = mask.getbbox()
    # the 16-row band with the most changed pixels (a band shows up as a run of rows)
    w = A.width
    row_counts = []
    mp = mask.load()
    for y in range(A.height):
        row_counts.append(sum(1 for x in range(0, w, 2) if mp[x, y]))
    best, best_y = -1, 0
    for y in range(0, max(1, A.height - 16), 4):
        c = sum(row_counts[y:y + 16])
        if c > best:
            best, best_y = c, y
    stat["worst_band"] = {"y0": best_y, "y1": best_y + 16, "changed_share": round(best / (16 * (w // 2 or 1)), 4)}
    first = next((y for y, c in enumerate(row_counts) if c > w // 20), None)
    last = next((y for y in range(len(row_counts) - 1, -1, -1) if row_counts[y] > w // 20), None)
    stat["rows_mostly_changed"] = [first, last]
    os.makedirs(out_dir, exist_ok=True)
    tag = tag or (os.path.splitext(os.path.basename(pa))[0] + "__" + os.path.splitext(os.path.basename(pb))[0])
    heat = ImageOps.autocontrast(G.point(lambda v: min(255, v * 4)), cutoff=0)
    heat = ImageOps.colorize(heat, black="black", white="white", mid="red")
    heat.save(os.path.join(out_dir, tag + "_heat.png"))
    side = Image.new("RGB", (A.width * 3, A.height))
    side.paste(A, (0, 0))
    side.paste(B, (A.width, 0))
    side.paste(heat, (A.width * 2, 0))
    side.save(os.path.join(out_dir, tag + "_ab.jpg"), quality=90)
    stat["heatmap"] = tag + "_heat.png"
    stat["side_by_side"] = tag + "_ab.jpg"
    return stat


def cmd_compare(a):
    st = compare(a.a, a.b, a.out_dir, a.threshold, a.rows)
    json.dump(st, open(os.path.join(a.out_dir, "compare.json"), "w"), indent=1)
    print(json.dumps(st, indent=1))
    return 0


def cmd_builds(a):
    sa, sb = load_session(a.session_a), load_session(a.session_b)
    out = os.path.join(a.session_b, "diffs", "vs_" + os.path.basename(os.path.normpath(a.session_a)))
    by_label_a = {}
    for x in sa["shots"]:
        by_label_a.setdefault(x.get("label") or "", []).append(x)
    pairs = []
    used = {}
    for i, x in enumerate(sb["shots"]):
        lab = x.get("label") or ""
        k = used.get(lab, 0)
        cand = by_label_a.get(lab, [])
        if lab and k < len(cand):
            pairs.append((cand[k], x))
            used[lab] = k + 1
        elif i < len(sa["shots"]):
            pairs.append((sa["shots"][i], x))
    res = []
    for xa, xb in pairs:
        st = compare(os.path.join(a.session_a, "shots", xa["file"]), os.path.join(a.session_b, "shots", xb["file"]),
                     out, a.threshold)
        res.append(st)
        print(f"{xa['file']} vs {xb['file']}: share>{a.threshold} {st['share_over']:.4f}, band {st['worst_band']}")
    json.dump({"a": sa.get("build_id"), "b": sb.get("build_id"), "pairs": res}, open(os.path.join(out, "compare.json"), "w"), indent=1)
    write_index(out, [(r["side_by_side"], f"share {r['share_over']:.4f} band {r['worst_band']}") for r in res])
    return 0


def write_index(d, items):
    rows = "\n".join(f'<figure><a href="{html.escape(f)}"><img src="{html.escape(f)}" loading="lazy"></a>'
                     f'<figcaption>{html.escape(f)} - {html.escape(c)}</figcaption></figure>' for f, c in items)
    open(os.path.join(d, "index.html"), "w").write(
        "<!doctype html><meta charset=utf-8><title>visual session</title><style>body{background:#111;color:#ddd;"
        "font:13px sans-serif}img{max-width:100%}figure{display:inline-block;width:32%;margin:4px}</style>" + rows)


def cmd_index(a):
    s = load_session(a.session)
    items = [(os.path.join("shots", x["file"]), f"{x.get('label')} {x.get('pressed')}") for x in s["shots"]]
    write_index(a.session, items)
    print(os.path.join(a.session, "index.html"))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    from switch_remote import switchcfg
    switchcfg.add_common_args(ap)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("new")
    p.add_argument("build_id")
    p.add_argument("test_session")
    p.add_argument("--toml")
    p.add_argument("--mode", choices=["visual", "perf"], default="visual")
    p.add_argument("--note")
    p = sub.add_parser("shoot")
    p.add_argument("session")
    p.add_argument("--count", type=int, default=1)
    p.add_argument("--every", type=float, default=2.0)
    p.add_argument("--wait", type=float, default=8.0)
    p.add_argument("--label", default="")
    p.add_argument("--combo", default="ZL,ZR,DDOWN", help="botbase button names; must match screenshot_buttons")
    p.add_argument("--also-botbase", action="store_true", help="also save sys-botbase's JPEG of the same moment")
    p = sub.add_parser("fetch")
    p.add_argument("session")
    p.add_argument("--label", default="")
    p = sub.add_parser("recordings")
    p.add_argument("session")
    p.add_argument("--src")
    p = sub.add_parser("frames")
    p.add_argument("video")
    p.add_argument("out_dir")
    p.add_argument("--every", type=float, default=1.0)
    p.add_argument("--at")
    p = sub.add_parser("compare")
    p.add_argument("a")
    p.add_argument("b")
    p.add_argument("out_dir")
    p.add_argument("--threshold", type=int, default=16)
    p.add_argument("--rows")
    p = sub.add_parser("builds")
    p.add_argument("session_a")
    p.add_argument("session_b")
    p.add_argument("--threshold", type=int, default=16)
    p = sub.add_parser("index")
    p.add_argument("session")
    a = ap.parse_args()
    return {"new": cmd_new, "shoot": cmd_shoot, "fetch": cmd_fetch, "recordings": cmd_recordings, "frames": cmd_frames,
            "compare": cmd_compare, "builds": cmd_builds, "index": cmd_index}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
