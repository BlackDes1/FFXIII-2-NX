#!/usr/bin/env python3
"""ffxiii2-nx: fetch the console's logs into logs/switch/<build_id>/<session>/ and analyse them.

  python tools/switch_logs/collect.py [--session NAME] [--all] [--no-parse]
  python tools/switch_logs/collect.py --monitor [--every 5] [--minutes 30]    (continuous polling, see monitor())

1. connects (sys-botbase file commands, FTP fallback), 2. reads <nro_dir>/deploy/current.json for the build id
("unknown" when the build was copied by hand), 3. lists <log_dir> and <log_dir>/rex (+ deko3d.log next to the NRO),
4. downloads the files that are new or whose size changed since the last collection (logs/switch/index.json; --all
takes every file), 5. checks each download: the size read back must equal fileSize before and after; a file that grew
while being read is re-read once and otherwise marked "growing" (the game was writing it), 6. stores it under
logs/switch/<build_id>/<session>/ with session.json (remote path, size, SHA-256, time, transport, build id). Earlier
sessions are never overwritten: a session name that exists gets a _2, _3 suffix. 7. runs parse_logs.py over the
session and writes report.txt + report.json next to the files.

Reading while the game runs: the renut log is appended by a writer thread with an fflush at least once a second
(sdk/src/core/logging.cpp), and rex_profile.log is reopened ("a") for each 10 s report, so a read sees the file up
to the last flush. Whether Horizon's FS lets sys-botbase open a file the game holds open for writing is checked on
every read: a failure is recorded as "locked" in session.json and the file is fetched again after the game closes.
The reads are opened read-only by sys-botbase and never touch the game's own handles.
"""
import argparse
import datetime
import hashlib
import json
import os
import posixpath
import sys
import time

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, TOOLS)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from switch_remote import sdfiles, switchcfg  # noqa: E402
import parse_logs  # noqa: E402


def remote_files(sd, cfg):
    out = []
    for d in (cfg["log_dir"], posixpath.join(cfg["log_dir"], "rex")):
        try:
            for e in sd.list(d):
                if e["type"] == "file" and (e["name"].endswith(".log") or e["name"].endswith(".txt")):
                    out.append((posixpath.join(d, e["name"]), e["size"]))
        except Exception as ex:  # noqa: BLE001
            print(f"  cannot list {d}: {ex}")
    try:
        for e in sd.list(cfg["nro_dir"]):
            if e["type"] == "file" and e["name"].endswith(".log"):
                out.append((posixpath.join(cfg["nro_dir"], e["name"]), e["size"]))
    except Exception:  # noqa: BLE001
        pass
    return out


def build_id(sd, cfg):
    try:
        return json.loads(sd.read(posixpath.join(cfg["nro_dir"], "deploy", "current.json")).decode())["build_id"]
    except Exception:  # noqa: BLE001
        return "unknown"


def load_index(root):
    try:
        with open(os.path.join(root, "index.json"), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def save_index(root, idx):
    os.makedirs(root, exist_ok=True)
    with open(os.path.join(root, "index.json"), "w", encoding="utf-8") as f:
        json.dump(idx, f, indent=1)


def fetch(sd, path):
    """(data, status): status ok | growing | locked."""
    for attempt in range(2):
        try:
            before = sd.size(path)
            data = sd.read(path)
            after = sd.size(path)
        except Exception as e:  # noqa: BLE001
            return None, f"locked ({e})"
        if len(data) == before == after:
            return data, "ok"
        if len(data) >= before and after >= len(data):
            status = "growing"
        else:
            status = f"incomplete ({len(data)} of {before}->{after})"
        if attempt == 0:
            time.sleep(1.5)
    return data, status


def session_dir(root, bid, name):
    base = os.path.join(root, bid, name)
    d, n = base, 2
    while os.path.exists(d):
        d = f"{base}_{n}"
        n += 1
    os.makedirs(d)
    return d


def collect(cfg, transport, session, take_all=False, parse=True, quiet=False):
    root = cfg["local_logs_dir"]
    idx = load_index(root)
    with sdfiles.open_sd(cfg, transport) as sd:
        bid = build_id(sd, cfg)
        files = remote_files(sd, cfg)
        todo = [(p, s) for p, s in files if take_all or idx.get(p, {}).get("size") != s]
        if not todo:
            if not quiet:
                print(f"no new or changed logs ({len(files)} files on the console)")
            return None, []
        d = session_dir(root, bid, session or datetime.datetime.now().strftime("%Y%m%d_%H%M%S"))
        meta = {"build_id": bid, "session": os.path.basename(d), "transport": sd.kind,
                "collected": datetime.datetime.now().isoformat(timespec="seconds"), "files": []}
        for p, size in todo:
            data, status = fetch(sd, p)
            rel = p[len(cfg["nro_dir"]):].lstrip("/") if p.startswith(cfg["nro_dir"]) else p.lstrip("/")
            entry = {"remote": p, "listed_size": size, "status": status}
            if data is not None:
                local = os.path.join(d, rel.replace("/", os.sep))
                os.makedirs(os.path.dirname(local), exist_ok=True)
                with open(local, "wb") as f:
                    f.write(data)
                entry.update(local=local, size=len(data), sha256=hashlib.sha256(data).hexdigest())
                if status in ("ok", "growing"):
                    idx[p] = {"size": len(data), "session": d}
            meta["files"].append(entry)
            if not quiet:
                print(f"  {status:9s} {entry.get('size', '-'):>9} {p}")
        with open(os.path.join(d, "session.json"), "w", encoding="utf-8") as f:
            json.dump(meta, f, indent=2)
    save_index(root, idx)
    new = [e for e in meta["files"] if "local" in e]
    if parse and new:
        out = parse_logs.analyse([e["local"] for e in new])
        text = parse_logs.report(out)
        with open(os.path.join(d, "report.txt"), "w", encoding="utf-8") as f:
            f.write(text + "\n")
        with open(os.path.join(d, "report.json"), "w", encoding="utf-8") as f:
            json.dump(out, f, indent=2)
        if not quiet:
            print(text)
    print(f"session: {d}")
    return d, new


def monitor(cfg, transport, every, minutes, session):
    """Polls every N s for new / grown logs and prints only what is new: new error/warning kinds, crashes, low fps."""
    seen_err, seen_warn = set(), set()
    end = time.time() + minutes * 60
    n = 0
    while time.time() < end:
        n += 1
        try:
            d, new = collect(cfg, transport, f"{session}_{n:04d}", parse=False, quiet=True)
        except Exception as e:  # noqa: BLE001
            print(time.strftime("%H:%M:%S"), "console not reachable:", e, flush=True)
            time.sleep(every)
            continue
        if new:
            out = parse_logs.analyse([e["local"] for e in new])
            for c in out["crash_hints"]:
                print(time.strftime("%H:%M:%S"), "CRASH", c, flush=True)
            for e in out["errors"]:
                if e["example"][:80] not in seen_err:
                    seen_err.add(e["example"][:80])
                    print(time.strftime("%H:%M:%S"), f"ERROR x{e['count']}", e["example"][:160], flush=True)
            for w in out["warnings"]:
                key = w["example"][:80]
                if key not in seen_warn:
                    seen_warn.add(key)
                    print(time.strftime("%H:%M:%S"), f"warn x{w['count']}", w["example"][:160], flush=True)
            for p in out["perf"][-3:]:
                print(time.strftime("%H:%M:%S"), f"perf {p['file']} t={p['t']:.0f}s {p['fps']:.1f} fps, render "
                      f"{p['render_ms']:.1f} ms, GPU {p['gpu_ms']:.1f} ms", flush=True)
            for a in out["anomalies"][-3:]:
                print(time.strftime("%H:%M:%S"), "ANOMALY", a, flush=True)
        time.sleep(every)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    switchcfg.add_common_args(ap)
    ap.add_argument("--session", help="name of the test session (default: the time)")
    ap.add_argument("--all", action="store_true", help="fetch every log, not only new / changed ones")
    ap.add_argument("--no-parse", action="store_true")
    ap.add_argument("--monitor", action="store_true", help="poll continuously")
    ap.add_argument("--every", type=float, default=5.0)
    ap.add_argument("--minutes", type=float, default=30.0)
    a = ap.parse_args()
    cfg = switchcfg.from_args(a)
    if a.monitor:
        monitor(cfg, a.transport, a.every, a.minutes, a.session or "monitor")
        return 0
    collect(cfg, a.transport, a.session, a.all, not a.no_parse)
    return 0


if __name__ == "__main__":
    sys.exit(main())
