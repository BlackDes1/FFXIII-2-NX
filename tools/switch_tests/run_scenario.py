#!/usr/bin/env python3
"""ffxiii2-nx: run a scripted test on the console through sys-botbase and keep what it shows.

  python tools/switch_tests/run_scenario.py tools/switch_tests/scenarios/idle_perf_60s.json [--session NAME]
  python tools/switch_tests/run_scenario.py --list

A scenario is JSON: {"name", "description", "precondition", "steps": [...]}. Steps ("do"):
  launch                       killGame if needed, then the config's launch_sequence; waits for a new renut log
  wait_new_log  timeout        until a renut_NNN.log newer than the one at the start of the test appears
  wait          s              sleep
  click         button [hold]  one press (sys-botbase click, 50 ms) - or held for hold seconds
  press/release button
  seq           seq            sys-botbase clickSeq string, e.g. "A,W1000,DDOWN,W500,A"
  stick         side x y s     tilt a stick for s seconds, then centre it
  shot          name           screenshot -> shots/<NN>_<name>.jpg
  shots         name every n   n screenshots every `every` seconds
  check_running                fails the test when no application is running any more (crash / closed)
  collect                      fetch the logs now (tools/switch_logs/collect.py) into this session
  note          text           a line in the result
Every button is released and both sticks centred when the test ends, also when it fails or is interrupted.
Results go to logs/switch/<build_id>/<session>/ (shots/, result.json, the logs, report.txt).
"""
import argparse
import datetime
import glob
import json
import os
import sys
import time

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, TOOLS)
sys.path.insert(0, os.path.join(TOOLS, "switch_logs"))
from switch_remote import botbase, sdfiles, switchcfg  # noqa: E402
import collect as logcollect  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


class TestFailed(RuntimeError):
    pass


def newest_log(cfg):
    with sdfiles.open_sd(cfg, "auto") as sd:
        n = 0
        for e in sd.list(cfg["log_dir"]):
            nm = e["name"]
            if nm.startswith("renut_") and nm.endswith(".log") and nm[6:-4].isdigit():
                n = max(n, int(nm[6:-4]))
        return n


def running(b):
    tid = b.title_id().strip()
    return bool(tid) and set(tid) != {"0"}


def run(cfg, scenario, session, transport):
    with sdfiles.open_sd(cfg, transport) as sd:
        bid = logcollect.build_id(sd, cfg)
    base = os.path.join(cfg["local_logs_dir"], bid, session)
    out_dir, n = base, 2
    while os.path.exists(out_dir):
        out_dir = f"{base}_{n}"
        n += 1
    shots = os.path.join(out_dir, "shots")
    os.makedirs(shots)
    result = {"scenario": scenario["name"], "build_id": bid, "session": os.path.basename(out_dir),
              "started": datetime.datetime.now().isoformat(timespec="seconds"), "steps": [], "status": "running"}
    t0 = time.time()
    log_at_start = newest_log(cfg)
    shot_no = [0]
    b = botbase.connect(cfg)

    def shot(name):
        shot_no[0] += 1
        p = os.path.join(shots, f"{shot_no[0]:02d}_{name}.jpg")
        try:
            data = b.screenshot()
            with open(p, "wb") as f:
                f.write(data)
            return p
        except Exception as e:  # noqa: BLE001
            return f"screenshot failed: {e}"

    try:
        for i, st in enumerate(scenario["steps"]):
            do = st["do"]
            ts = time.time()
            info = ""
            if do == "launch":
                if cfg.get("launch_method", "tile") == "tile":
                    from switch_remote import home_launch
                    b.close()
                    rc = home_launch.launch(cfg, kill=True, wait_s=float(st.get("timeout", cfg.get("launch_wait_s", 90))))
                    b = botbase.connect(cfg)
                    if rc != 0:
                        raise TestFailed(f"home_launch failed (exit {rc})")
                    info += "started from the HOME tile; "
                    st = dict(st, do="wait_new_log", timeout=5)
                else:
                    if running(b):
                        info += "killGame " + b.kill_game() + "; "
                        time.sleep(3)
                        b.click("A")
                        time.sleep(2)
                    for s in cfg.get("launch_sequence") or []:
                        b.click_seq(s)
                    if not cfg.get("launch_sequence"):
                        info += "launch_sequence empty: start the game by hand; "
                    st = dict(st, do="wait_new_log", timeout=st.get("timeout", cfg.get("launch_wait_s", 90)))
            if st["do"] == "wait_new_log":
                end = time.time() + float(st.get("timeout", 90))
                while True:
                    cur = newest_log(cfg)
                    if cur > log_at_start:
                        info += f"renut_{cur:03d}.log appeared"
                        break
                    if time.time() > end:
                        raise TestFailed(f"no new renut log within {st.get('timeout', 90)} s (did the game start?)")
                    time.sleep(3)
            elif do == "wait":
                time.sleep(float(st["s"]))
            elif do == "click":
                if st.get("hold"):
                    b.press(st["button"])
                    time.sleep(float(st["hold"]))
                    b.release(st["button"])
                else:
                    b.click(st["button"])
            elif do == "press":
                b.press(st["button"])
            elif do == "release":
                b.release(st["button"])
            elif do == "seq":
                b.click_seq(st["seq"])  # waits until the sequence has run
            elif do == "stick":
                b.stick(st["side"], st["x"], st["y"])
                time.sleep(float(st.get("s", 1)))
                b.stick(st["side"], 0, 0)
            elif do == "shot":
                info = shot(st.get("name", f"step{i}"))
            elif do == "shots":
                got = []
                for k in range(int(st.get("n", 3))):
                    got.append(shot(f"{st.get('name', 'shot')}_{k + 1}"))
                    if k + 1 < int(st.get("n", 3)):
                        time.sleep(float(st.get("every", 5)))
                info = f"{len(got)} screenshots"
            elif do == "check_running":
                if not running(b):
                    raise TestFailed("the application is not running any more (crash or closed)")
                info = "running"
            elif do == "collect":
                d, new = logcollect.collect(cfg, transport, os.path.basename(out_dir) + "_logs", quiet=True)
                info = f"{len(new)} logs -> {d}"
            elif do == "note":
                info = st.get("text", "")
            elif do != "wait_new_log":
                raise TestFailed(f"unknown step {do!r}")
            result["steps"].append({"step": i, "do": do, "t": round(ts - t0, 2), "took": round(time.time() - ts, 2),
                                    "info": info})
            print(f"[{time.time() - t0:7.1f}s] {do:13s} {info}", flush=True)
        result["status"] = "passed"
    except TestFailed as e:
        result["status"] = "failed"
        result["error"] = str(e)
        result["failure_shot"] = shot("failure")
        print("FAILED:", e)
    except KeyboardInterrupt:
        result["status"] = "interrupted"
    finally:
        try:
            b.release_all()
            for name in botbase.BUTTONS:
                b.send(f"release {name}")
        except Exception:  # noqa: BLE001
            pass
        try:
            result["application_running_at_end"] = running(b)
        except Exception:  # noqa: BLE001
            result["application_running_at_end"] = None
        b.sock.close()
        b.sock = None
        result["duration_s"] = round(time.time() - t0, 1)
        try:
            d, new = logcollect.collect(cfg, transport, os.path.basename(out_dir) + "_final", quiet=True)
            result["logs"] = d
        except Exception as e:  # noqa: BLE001
            result["logs"] = f"not collected: {e}"
        with open(os.path.join(out_dir, "result.json"), "w", encoding="utf-8") as f:
            json.dump(result, f, indent=2)
    print(f"{result['status']} in {result['duration_s']} s -> {out_dir}")
    return 0 if result["status"] == "passed" else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    switchcfg.add_common_args(ap)
    ap.add_argument("scenario", nargs="?")
    ap.add_argument("--session")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    if a.list or not a.scenario:
        for p in sorted(glob.glob(os.path.join(HERE, "scenarios", "*.json"))):
            s = json.load(open(p, encoding="utf-8"))
            print(f"{os.path.basename(p):32s} {s['description']}")
        return 0
    cfg = switchcfg.from_args(a)
    path = a.scenario if os.path.isfile(a.scenario) else os.path.join(HERE, "scenarios", a.scenario)
    scenario = json.load(open(path, encoding="utf-8"))
    session = a.session or f"{scenario['name']}_{datetime.datetime.now():%Y%m%d_%H%M%S}"
    return run(cfg, scenario, session, a.transport)


if __name__ == "__main__":
    sys.exit(main())
