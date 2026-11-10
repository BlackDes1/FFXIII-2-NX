#!/usr/bin/env python3
"""ffxiii2-nx: drive the running game from its start to the Chaos Bahamut battle by what the screen shows.

  python tools/switch_tests/nav_ffxiii2.py [--session NAME] [--timeout 420]

Fixed waits drifted by tens of seconds between boots (2026-11-05: the title menu came 30-60 s after "Press any
button", so presses landed on the wrong screens). This looks at each screenshot instead. States, by region means of
the 1280x720 screenshot (calibrated on the 2026-11-05 console shots; "b-r" = blue minus red):
  title menu    whole frame mean > 150 (white-pink artwork)
  load screen   save tiles band (280-1000, 140-260) mean 25-50, whole frame < 40, HUD ring < 10
  load dialog   ... plus the dialog area (540-740, 330-380) b-r > 40 ("Load this data?", "Data loaded.")
  battle        HUD ring (60-200, 520-660) mean > 55 and b-r > 12, no dialog
  battle dialog HUD ring mean > 25 and dialog area b-r > 45 (the tutorial prompt: DD + A = Skip)
Everything else (logos, the opening movie, "Press any button", the autosave notice, cutscenes) gets A, and every
third time the cutscene skip (START, BACK, A) once the save is loaded. The path only reads the save (Save Data 01,
the first tile); the game autosaves later in the opening: back up first (tools/switch_remote/backup_saves.py).
Screenshots of every decision go to logs/switch/<build>/<session>/nav/. Exit 0 = in the battle, HUD up, no dialog.
"""
import argparse
import datetime
import io
import os
import sys
import time

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, TOOLS)
sys.path.insert(0, os.path.join(TOOLS, "switch_logs"))
from switch_remote import botbase, sdfiles, switchcfg  # noqa: E402
import collect as logcollect  # noqa: E402
from PIL import Image, ImageStat  # noqa: E402

ROI = {"all": (0, 0, 1280, 720), "tiles": (280, 140, 1000, 260), "hud": (60, 520, 200, 660),
       "dlg": (540, 330, 740, 380)}


def features(jpg):
    im = Image.open(io.BytesIO(jpg)).convert("RGB").resize((1280, 720))
    f = {}
    for k, box in ROI.items():
        r, g, b = ImageStat.Stat(im.crop(box)).mean
        f[k] = (r + g + b) / 3.0
        f[k + "_br"] = b - r
    return f


def state(f):
    if f["all"] > 150:
        return "title_menu"
    if 25 <= f["tiles"] <= 50 and f["all"] < 40 and f["hud"] < 10:
        return "load_dialog" if f["dlg_br"] > 40 else "load_screen"
    if f["hud"] > 25 and f["dlg_br"] > 45:
        return "battle_dialog"
    if f["hud"] > 55 and f["hud_br"] > 12:
        return "battle"
    return "other"


def navigate(cfg, out_dir, timeout=420.0, log=print, phase="to_title"):
    # 2026-11-07 (k244): phase "opening" continues a run that intro_ffxiii2.py started with NEW GAME (Save Data 01 is no
    # longer the opening save on the console)
    os.makedirs(out_dir, exist_ok=True)
    t0 = time.time()
    other_count = 0
    battle_seen = 0
    n = 0
    with botbase.connect(cfg) as b:
        # ffxiii2-nx (2026-11-06, k202): a failed start left the console on HOME and this pressed A / PLUS there for
        # 55 s (HOME looked like the title menu). Press only while FF XIII-2's forwarder is the running application.
        want = str(cfg.get("title_id", "055B4BDC705F6000")).upper()
        tid = ""
        for _ in range(4):  # getTitleID can time out right after a start: ask again
            try:
                tid = b.title_id().strip().upper()
                break
            except (OSError, botbase.BotBaseError):
                time.sleep(3)
                b.close()
                b.connect()
        if tid != want:
            log(f"not navigating: the running application is {tid or 'none'}, not FF XIII-2 ({want})")
            return 7
        while time.time() - t0 < timeout:
            # 2026-11-06 11:24: one pixelPeek came back without a JPG and ended the navigation (the recording then took
            # the title screen): retry on a fresh connection, up to 3 times
            jpg = None
            for _ in range(3):
                try:
                    jpg = b.screenshot()
                    break
                except (OSError, botbase.BotBaseError) as e:
                    log(f"screenshot failed ({e}): retrying")
                    time.sleep(2)
                    b.close()
                    b.connect()
            if jpg is None:
                log("no screenshot after 3 tries: stopping")
                return 8
            try:
                f = features(jpg)
            except OSError as e:  # k258: a truncated JPEG from the console (k258_ab run): take the next shot
                log(f"screenshot unreadable ({e}): next one")
                time.sleep(1)
                continue
            st = state(f)
            # 2026-11-05: Chaos Bahamut's white flash attack (whole frame > 150) looked like the title menu once the save
            # was loaded, and PLUS, A went to the battle's menus. After the load the title cannot come back.
            if st == "title_menu" and phase in ("opening", "battle"):
                st = "other"
            n += 1
            with open(os.path.join(out_dir, f"{n:03d}_{phase}_{st}.jpg"), "wb") as fh:
                fh.write(jpg)
            log(f"[{time.time() - t0:6.1f}s] {phase:10s} screen {st:13s} all {f['all']:5.1f} tiles {f['tiles']:5.1f} "
                f"hud {f['hud']:5.1f}/{f['hud_br']:+5.1f} dlg b-r {f['dlg_br']:+5.1f}")
            if st == "title_menu":
                b.click_seq("PLUS,W2500,A,W5000")
                phase = "load"
            elif st == "load_screen":
                b.click_seq("A,W3000")  # Save Data 01 (the first tile, selected) -> "Load this data?"
                phase = "load"
            elif st == "load_dialog":
                b.click_seq("A,W4000")  # Yes / "Data loaded." OK
                phase = "opening"
            elif st == "battle_dialog":
                b.click_seq("DD,W600,A,W2500")  # the tutorial prompt: Skip
                phase = "battle"
                battle_seen = 0
            elif st == "battle":
                # 2026-11-06 (k197, with the audio working): the tutorial prompt came a few seconds after the HUD, so a
                # single battle screen ended the navigation in front of it. Two in a row (about 4 s) now: three let
                # the battle run on into Bahamut's long white attack scene before the shots (k197b).
                battle_seen += 1
                if battle_seen >= 2:
                    log(f"in the battle after {time.time() - t0:.0f} s")
                    return 0
                continue
            else:
                battle_seen = 0
                other_count += 1
                if phase == "opening" and other_count % 3 == 0:
                    b.click_seq("PLUS,W2000,MINUS,W2000,A,W3000")  # skip the cutscene (pause, BACK = skip, A = yes)
                else:
                    b.click_seq("A,W3000")
    log("timeout: the battle was not reached")
    return 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    switchcfg.add_common_args(ap)
    ap.add_argument("--session", default=None)
    ap.add_argument("--timeout", type=float, default=420)
    ap.add_argument("--phase", default="to_title", choices=["to_title", "load", "opening"],
                    help="opening = after intro_ffxiii2.py's NEW GAME: skip cutscenes until the battle")
    a = ap.parse_args()
    cfg = switchcfg.from_args(a)
    with sdfiles.open_sd(cfg, a.transport) as sd:
        bid = logcollect.build_id(sd, cfg)
    session = a.session or f"nav_{datetime.datetime.now():%Y%m%d_%H%M%S}"
    out = os.path.join(cfg["local_logs_dir"], bid, session, "nav")
    return navigate(cfg, out, a.timeout, log=lambda s: print(s, flush=True), phase=a.phase)


if __name__ == "__main__":
    sys.exit(main())
