#!/usr/bin/env python3
"""ffxiii2-nx: start Final Fantasy XIII-2 from the console's HOME menu, wherever its tile is.

  python tools/switch_remote/home_launch.py [--kill] [--wait 120]

The HOME menu orders the tiles by last use and wraps around, so a fixed button sequence starts the wrong game
(2026-11-05: it started Zelda Dusklight). This goes to the tile row, then steps left one tile at a time and compares the
selected tile (inside the cyan selection frame) with ref_tile_ffxiii2.png (48x48 grey, made from a screenshot with the
FF XIII-2 forwarder selected): mean difference 0 for that tile, 87-107 for the others. On a match it presses A and waits
for a new logs/renut_NNN.log. Needs PIL (the PC's C:\\Python314 has it). Exit codes: 0 started, 3 tile not found,
4 no new log, 5 an application is running (pass --kill to close it first).
"""
import argparse
import io
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from switch_remote import botbase, sdfiles, switchcfg  # noqa: E402

try:
    from PIL import Image, ImageChops, ImageStat
except ImportError:  # pragma: no cover
    Image = None

REF = os.path.join(HERE, "ref_tile_ffxiii2.png")
# ffxiii2-nx (2026-11-06, k202): after a reboot HOME showed the forwarder's tile without its icon (a grey console with
# "?", the name "Final Fantasy XIII-2" above it); that tile is the second reference. Because a missing icon looks the
# same for any title, a start is only accepted when the running application is then FF XIII-2's forwarder.
REFS = [REF, os.path.join(HERE, "ref_tile_ffxiii2_noicon.png")]
TITLE_ID = "055B4BDC705F6000"
MATCH = 20.0  # mean grey difference below this = the FF XIII-2 tile


def _cyan(p):
    r, g, b = p[:3]
    return r < 120 and g > 170 and b > 170 and abs(g - b) < 60


def selection_box(im):
    """(x0, y0, x1, y1) of the cyan frame around the selected tile. The frame's sides are columns that are cyan over
    most of the tile row's height; tile art with cyan in it (Wind Waker's sky) can add such columns, so the columns
    are grouped and the pair about one tile apart (240-300 px at 1280x720) is taken. The top edge is searched below
    the cyan title text that sits above the frame."""
    im = im.convert("RGB")
    w, h = im.size
    px = im.load()
    y_lo, y_hi = int(h * 0.27), int(h * 0.62)
    n = len(range(y_lo, y_hi, 3))
    cols = [x for x in range(w) if sum(_cyan(px[x, y]) for y in range(y_lo, y_hi, 3)) > n * 0.8]
    groups = []
    for x in cols:
        if groups and x - groups[-1][-1] <= 2:
            groups[-1].append(x)
        else:
            groups.append([x])
    scale = w / 1280.0
    for i, g in enumerate(groups):
        for g2 in groups[i + 1:]:
            if 240 * scale <= g2[-1] - g[0] <= 300 * scale:
                x0, x1 = g[0], g2[-1]
                xm = x0 + int(20 * scale)
                rows = [y for y in range(int(h * 0.24), int(h * 0.7)) if _cyan(px[xm, y])]
                if rows:
                    return x0, rows[0], x1, rows[-1]
    return None


def tile_signature(im, box):
    x0, y0, x1, y1 = box
    return im.convert("L").crop((x0 + 14, y0 + 14, x1 - 14, y1 - 14)).resize((48, 48))


def difference(jpg_bytes, ref):
    im = Image.open(io.BytesIO(jpg_bytes))
    box = selection_box(im)
    if not box or box[2] - box[0] < 150:
        return None, box
    return ImageStat.Stat(ImageChops.difference(tile_signature(im, box), ref)).mean[0], box


def newest_log(cfg):
    with sdfiles.open_sd(cfg, "botbase") as sd:
        n = 0
        for e in sd.list(cfg["log_dir"]):
            nm = e["name"]
            if nm.startswith("renut_") and nm.endswith(".log") and nm[6:-4].isdigit():
                n = max(n, int(nm[6:-4]))
        return n


def launch(cfg, kill=False, wait_s=120, max_steps=40, verbose=True):
    if Image is None:
        raise SystemExit("PIL is missing: home_launch needs it")
    refs = [Image.open(p).convert("L") for p in REFS if os.path.exists(p)]
    want = str(cfg.get("title_id", TITLE_ID)).upper()
    before = newest_log(cfg)
    with botbase.connect(cfg) as b:
        tid = b.title_id().strip()
        if tid and set(tid) != {"0"}:
            if not kill:
                print(f"an application is running ({tid}): pass --kill")
                return 5
            print("killGame:", b.kill_game())
            time.sleep(3)
            b.click("A")  # the "software was closed" notice
            time.sleep(2.5)
        found = False
        # ffxiii2-nx (2026-11-06 11:46): HOME showed the FF XIII-2 tile selected (difference 0 offline on that very
        # screenshot) and still 6 steps after HOME + DD,DU found no selection frame. Look first, press nothing when the
        # tile is already selected; the screenshots of steps without a frame are kept for the next diagnosis.
        dump = os.path.join(os.path.dirname(os.path.dirname(HERE)), "logs", "home_launch")
        try:
            jpg = b.screenshot()
            ds = [difference(jpg, r) for r in refs]
            d, box = min(ds, key=lambda t: 1e9 if t[0] is None else t[0])
            if verbose:
                print(f"  before any press: selected tile {box}, difference {d if d is None else round(d, 1)}", flush=True)
            found = d is not None and d < MATCH
        except botbase.BotBaseError as e:  # noqa: PERF203
            print(f"  first screenshot failed ({e})")
        if not found:
            b.click("HOME")
            time.sleep(1.5)
            b.click_seq("DD,W600,DU,W900")  # the tile row, from wherever the cursor was
        no_box = 0
        for step in range(0 if found else max_steps):
            jpg = b.screenshot()
            ds = [difference(jpg, r) for r in refs]
            d, box = min(ds, key=lambda t: 1e9 if t[0] is None else t[0])
            if verbose:
                print(f"  step {step}: selected tile {box}, difference {d if d is None else round(d, 1)}", flush=True)
            if d is not None and d < MATCH:
                found = True
                break
            # ffxiii2-nx (2026-11-06, k202): 40 D-pad presses went to a screen without the tile row. No selection
            # frame: HOME once after 3 such screens, give up after 6 (no more blind presses).
            no_box = no_box + 1 if box is None else 0
            if box is None:
                os.makedirs(dump, exist_ok=True)
                with open(os.path.join(dump, time.strftime("%Y%m%d_%H%M%S") + f"_step{step}.jpg"), "wb") as fh:
                    fh.write(jpg)
            if no_box == 3:
                b.click("HOME")
                time.sleep(1.5)
                b.click_seq("DD,W600,DU,W900")
                continue
            if no_box >= 6:
                print("no HOME tile row on screen: not pressing on")
                return 3
            b.click("DLEFT")
            time.sleep(0.6)
        if not found:
            print("FF XIII-2 tile not found on HOME")
            return 3
        b.click("A")
        # the application that started must be FF XIII-2's forwarder; otherwise close it at once
        tid = ""
        picker_a = False
        for i in range(15):
            time.sleep(1.0)
            tid = b.title_id().strip().upper()
            if tid and set(tid) != {"0"}:
                break
            # ffxiii2-nx (2026-11-06 11:10): after a reboot HOME asks for a user after A on the tile ("Waehle einen
            # Nutzer aus", the first user highlighted). Nothing runs yet: one A takes the highlighted user. The
            # forwarder keeps its saves on the SD card, so the user does not choose a save.
            if i == 3 and not picker_a:
                print("no application after the tile's A: A for the highlighted user (user picker)")
                b.click("A")
                picker_a = True
        if tid != want:
            print(f"the started application is {tid or 'none'}, not FF XIII-2 ({want})" +
                  (": closing it" if tid and set(tid) != {"0"} else ""))
            if tid and set(tid) != {"0"}:
                print("killGame:", b.kill_game())
            return 6
    end = time.time() + wait_s
    while time.time() < end:
        time.sleep(4)
        try:
            n = newest_log(cfg)
        except Exception:  # noqa: BLE001
            continue
        if n > before:
            print(f"started: renut_{n:03d}.log")
            return 0
    print(f"no new renut log within {wait_s} s")
    return 4


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    switchcfg.add_common_args(ap)
    ap.add_argument("--kill", action="store_true", help="close a running application first")
    ap.add_argument("--wait", type=float, default=120)
    a = ap.parse_args()
    return launch(switchcfg.from_args(a), a.kill, a.wait)


if __name__ == "__main__":
    sys.exit(main())
