#!/usr/bin/env python3
"""ffxiii2-nx (k239, D-009 on the Switch): the PC intro run's input script on the console.

  python tools/switch_tests/intro_ffxiii2.py --out out/k239_intro [--timeout 240] [--after 150]

Waits (by the screenshots) for the title menu, pressing A on everything before it ("Press any button", logos). The
logos fade through pure white, so the menu is a frame mean of 170-245 on two screenshots in a row (the menu with
CONTINUE selected: 218). Then DUP (NEW GAME; with saves on the console the cursor starts on CONTINUE) and A, and
after that the PC script's presses at its times relative to its NEW GAME A at 43 s (tools/run_pc.ps1 intro script:
START 60, BACK 62, A 64 / 70 / 76 / 82 / 88), a screenshot every 5 s, the game kept running for --after seconds from
the NEW GAME press. NEW GAME writes no save by itself; back the saves up first (backup_saves.py) all the same.
Presses only while FF XIII-2 is the running application. Exit 0 = the script ran to its end.
"""
import argparse
import os
import sys
import time

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, TOOLS)
sys.path.insert(0, os.path.join(TOOLS, "switch_tests"))
from switch_remote import botbase, switchcfg  # noqa: E402
from nav_ffxiii2 import features  # noqa: E402

# (seconds after NEW GAME's A, button). k239g's console timeline: the save screen ("Create new save data", its first
# tile an empty slot: a new save, no existing one written) and the autosave notice to ~40 s, the opening movie from
# ~60 s. The A presses to 45 s take the save screens and the notice (as in k239g); the movie is skipped at 68 s (START,
# BACK, A) and the PC's A presses after its skip follow at its spacing.
SCRIPT = [(21, "A"), (27, "A"), (33, "A"), (39, "A"), (45, "A"), (68, "PLUS"), (70, "MINUS"), (72, "A"),
          (78, "A"), (84, "A"), (90, "A"), (96, "A")]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    switchcfg.add_common_args(ap)
    ap.add_argument("--out", required=True)
    ap.add_argument("--timeout", type=float, default=240)
    ap.add_argument("--after", type=float, default=150)
    ap.add_argument("--burst-from", type=float, default=0, help="screenshots back to back from this time (s)")
    ap.add_argument("--burst-to", type=float, default=0, help="... to this time (s after NEW GAME)")
    ap.add_argument("--force-presses", action="store_true",
                    help="press after the menu even when getTitleID does not name FF XIII-2 (Frame Tap attached)")
    a = ap.parse_args()
    cfg = switchcfg.from_args(a)
    os.makedirs(a.out, exist_ok=True)
    want = str(cfg.get("title_id", "055B4BDC705F6000")).upper()
    t0 = time.time()
    with botbase.connect(cfg) as b:
        def shot(name):
            for _ in range(3):
                try:
                    jpg = b.screenshot()
                    with open(os.path.join(a.out, name + ".jpg"), "wb") as fh:
                        fh.write(jpg)
                    return jpg
                except (OSError, botbase.BotBaseError) as e:
                    print(f"screenshot failed ({e})", flush=True)
                    time.sleep(2)
                    b.close()
                    b.connect()
            return None

        def ours():
            try:
                tid = b.title_id().strip().upper()
            except (OSError, botbase.BotBaseError) as e:
                print(f"getTitleID failed ({e}): pressing", flush=True)
                return True  # a timeout right after a start: keep going
            if tid == want:
                return True
            # k239j: with Frame Tap's stream attached the presses after the menu were all skipped; say what came back
            print(f"title id {tid or 'none'}{': pressing anyway (--force-presses)' if a.force_presses else ': not pressing'}",
                  flush=True)
            return a.force_presses

        n = 0
        seen = 0
        while True:
            if time.time() - t0 > a.timeout:
                print("timeout: no title menu", flush=True)
                return 1
            if not ours():
                print("FF XIII-2 is not running", flush=True)
                return 7
            jpg = shot(f"pre_{n:03d}")
            n += 1
            if jpg is None:
                return 8
            m = features(jpg)["all"]
            menu = 170 <= m <= 245
            print(f"[{time.time() - t0:6.1f}s] mean {m:5.1f}{' menu' if menu else ''}", flush=True)
            if menu:
                seen += 1
                if seen >= 2:
                    break
                time.sleep(1.5)
                continue
            seen = 0
            if m > 245:  # a logo's white flash: wait, do not press into the menu
                time.sleep(1.5)
                continue
            b.click_seq("A,W2500")
        # NEW GAME, NORMAL MODE, then "Begin game in NORMAL MODE?" (the cursor on No): UP to Yes; its A is t = 0
        b.click_seq("DUP,W1000,A,W2500,A,W2500,DUP,W1000")
        shot("menu_new_game")
        b.click("A")
        ts = time.time()
        print(f"[{ts - t0:6.1f}s] NEW GAME (t = 0)", flush=True)
        events = sorted(SCRIPT)
        next_shot = 5.0
        while True:
            t = time.time() - ts
            if events and t >= events[0][0]:
                at, button = events.pop(0)
                if ours():
                    b.click(button)
                    print(f"t {t:6.1f}: {button}", flush=True)
            if a.burst_to and a.burst_from <= t < a.burst_to and not (events and t + 1.5 >= events[0][0]):
                shot(f"b{t:07.2f}")  # k239: back to back (~0.5-1 s each) over the real-time opening
                continue
            if t >= next_shot:
                shot(f"t{int(next_shot):03d}")
                next_shot += 5.0
            if t >= a.after and not events:
                break
            time.sleep(0.1)
    print("script done", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
