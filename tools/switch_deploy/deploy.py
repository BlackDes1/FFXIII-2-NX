#!/usr/bin/env python3
"""ffxiii2-nx: put a build on the console over the network, with backups and checks.

  python tools/switch_deploy/deploy.py deploy --package <Works/builds/<stamp>_ffxiii2_rcN> [--slot current|experimental]
                                              [--restart] [--full] [--dry-run]
  python tools/switch_deploy/deploy.py deploy --nro app/out/switch/ffxiii2.nro --elf <elf> [--toml f] [--cache f]
  python tools/switch_deploy/deploy.py status          what the console runs (deploy/current.json) and the local state
  python tools/switch_deploy/deploy.py mark-good       the current build becomes the last known-good one
  python tools/switch_deploy/deploy.py mark-failed     the current build is recorded as failed (its files stay live)
  python tools/switch_deploy/deploy.py rollback        put the last known-good build back

What deploy does, in order (LIVE_SWITCH_TESTING.md, 8):
 1. Finds the NRO and what goes with it (ffxiii2.toml, deko3d_shaders.bin; configs/ when --package), and the
    unstripped ELF.
 2. Build id = <YYYYmmdd_HHMM>_<first 12 hex of the NRO's SHA-256>; SHA-256 of every file.
 3. Keeps the build on the PC: <local_builds_dir>/<slot>/<build id>/ (NRO, ELF, toml, manifest.json; the cache only
    by hash, it is 50+ MB).
 4. Connects (sys-botbase files, FTP fallback), reads what is installed (deploy/current.json on the SD card; for a
    console set up by hand, the sizes of the live files).
 5. Refuses to run while an application is open unless --restart (then killGame first).
 6. Backs up: every live file that will change is MOVED (rename, no copy) to
    <nro_dir>/deploy/<known_good|experimental>/<old build id>/. Saves (renut/), logs/, before_rc*/ are never touched.
 7. Uploads only the files whose SHA-256 differs (--full: all), each via <name>.part, size + SHA-256 read back, rename.
 8. Writes deploy/current.json and appends deploy/history.log on the SD card, state.json on the PC.
 9. With --restart: launches with launch_sequence from the config and waits for a new renut_NNN.log (the proof that
    the new build started), then collects the first logs (tools/switch_logs/collect.py).
"""
import argparse
import datetime
import hashlib
import json
import os
import posixpath
import shutil
import sys
import time

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, TOOLS)
from switch_remote import botbase, sdfiles, switchcfg  # noqa: E402

LIVE_FILES = ("ffxiii2.nro", "ffxiii2.toml", "deko3d_shaders.bin", "deko3d_glsl.bin")
SLOTS = ("known_good", "current", "experimental", "failed")


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def find_files(a):
    files = {}
    elf = a.elf
    if a.package:
        sd = os.path.join(a.package, "COPY_TO_SD_switch_ffxiii2-nx")
        for name in LIVE_FILES:
            p = os.path.join(sd, name)
            if os.path.isfile(p):
                files[name] = p
        cfgdir = os.path.join(sd, "configs")
        if os.path.isdir(cfgdir):
            for n in sorted(os.listdir(cfgdir)):
                if n.endswith(".toml"):
                    files["configs/" + n] = os.path.join(cfgdir, n)
        if not elf:
            elfs = [n for n in os.listdir(a.package) if n.endswith(".elf")]
            elf = os.path.join(a.package, elfs[0]) if elfs else None
    if a.nro:
        files["ffxiii2.nro"] = a.nro
    if a.toml:
        files["ffxiii2.toml"] = a.toml
    if a.cache:
        files["deko3d_shaders.bin"] = a.cache
    if a.glsl:  # the complete GLSL package beside the NRO (2026-11-05: 24,920 shaders, 80 MB, read on use)
        files["deko3d_glsl.bin"] = a.glsl
    if "ffxiii2.nro" not in files:
        raise SystemExit("no NRO: give --package or --nro")
    if not elf or not os.path.isfile(elf):
        raise SystemExit("the unstripped ELF of this NRO is required (--elf): profiler reports need it")
    return files, elf


def manifest_for(files, elf, slot, name):
    nro_hash = sha256_file(files["ffxiii2.nro"])
    build_id = datetime.datetime.now().strftime("%Y%m%d_%H%M") + "_" + nro_hash[:12]
    m = {"build_id": build_id, "name": name or "", "slot": slot, "created": datetime.datetime.now().isoformat(timespec="seconds"),
         "files": {}, "elf": {"name": os.path.basename(elf), "sha256": sha256_file(elf), "size": os.path.getsize(elf)}}
    for rel, p in files.items():
        m["files"][rel] = {"sha256": nro_hash if rel == "ffxiii2.nro" else sha256_file(p), "size": os.path.getsize(p),
                           "source": os.path.abspath(p)}
    return m


def archive_local(cfg, m, files, elf):
    d = os.path.join(cfg["local_builds_dir"], m["slot"], m["build_id"])
    os.makedirs(d, exist_ok=True)
    for rel, p in files.items():
        if rel in ("deko3d_shaders.bin", "deko3d_glsl.bin"):
            continue  # big data files: kept by hash (manifest) only
        dst = os.path.join(d, rel.replace("/", os.sep))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(p, dst)
    shutil.copy2(elf, os.path.join(d, os.path.basename(elf)))
    with open(os.path.join(d, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(m, f, indent=2)
    return d


def state_path(cfg):
    return os.path.join(cfg["local_builds_dir"], "state.json")


def load_state(cfg):
    try:
        with open(state_path(cfg), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {"current": None, "known_good": None, "experimental": [], "failed": [], "history": []}


def save_state(cfg, st):
    os.makedirs(cfg["local_builds_dir"], exist_ok=True)
    with open(state_path(cfg), "w", encoding="utf-8") as f:
        json.dump(st, f, indent=2)


def remote_current(sd, cfg):
    p = posixpath.join(cfg["nro_dir"], "deploy", "current.json")
    try:
        return json.loads(sd.read(p).decode("utf-8"))
    except Exception:  # noqa: BLE001 - not deployed by this tool yet
        live = {}
        try:
            for e in sd.list(cfg["nro_dir"]):
                if e["type"] == "file" and e["name"] in LIVE_FILES:
                    live[e["name"]] = {"size": e["size"], "sha256": None}
        except Exception:  # noqa: BLE001
            pass
        return {"build_id": "manual_" + datetime.datetime.now().strftime("%Y%m%d_%H%M"), "slot": "known_good",
                "files": live, "note": "installed by hand (no deploy/current.json); kept as known-good"}


def app_running(cfg):
    try:
        with botbase.connect(cfg) as b:
            tid = b.title_id().strip()
            return bool(tid) and set(tid) != {"0"}, tid
    except OSError:
        return False, "?"


def append_history(sd, cfg, line):
    p = posixpath.join(cfg["nro_dir"], "deploy", "history.log")
    data = (line + "\n").encode("utf-8")
    if sd.kind == "botbase":
        sd.b.file_append(p, data)
    else:
        old = sd.read(p) if sd.exists(p) else b""
        sd.f.write(p, old + data)


def newest_log_number(sd, cfg):
    best = 0
    for e in sd.list(cfg["log_dir"]):
        n = e["name"]
        if n.startswith("renut_") and n.endswith(".log") and n[6:-4].isdigit():
            best = max(best, int(n[6:-4]))
    return best


def launch(cfg, sd, before):
    """Starts the game: launch_method "tile" (default when PIL and ref_tile_ffxiii2.png are there) finds the FF XIII-2
    tile on HOME by its picture (switch_remote/home_launch.py); "sequence" presses launch_sequence; otherwise it waits for
    a manual start. Returns the new renut log number or None."""
    method = cfg.get("launch_method", "tile")
    if method == "tile":
        from switch_remote import home_launch
        rc = home_launch.launch(cfg, kill=False, wait_s=float(cfg.get("launch_wait_s", 90)))
        return newest_log_number(sd, cfg) if rc == 0 else None
    seq = cfg.get("launch_sequence") or []
    if method != "sequence" or not seq:
        print("no automatic launch configured: start the game on the console by hand now "
              f"(waiting {cfg['launch_wait_s']} s for a new renut log)")
    else:
        with botbase.connect(cfg) as b:
            for step in seq:
                b.click_seq(step)
    end = time.time() + float(cfg.get("launch_wait_s", 90))
    while time.time() < end:
        time.sleep(5)
        try:
            n = newest_log_number(sd, cfg)
        except Exception:  # noqa: BLE001
            continue
        if n > before:
            return n
    return None


def cmd_deploy(a, cfg):
    files, elf = find_files(a)
    m = manifest_for(files, elf, a.slot, a.name)
    print(f"build {m['build_id']}  ({len(files)} files, NRO {m['files']['ffxiii2.nro']['size']} bytes)")
    for rel, f in m["files"].items():
        print(f"  {f['sha256'][:16]}  {f['size']:>10}  {rel}")
    local = archive_local(cfg, m, files, elf)
    print("kept on the PC:", local)
    if a.dry_run:
        return 0
    running, tid = app_running(cfg)
    if running and not a.restart:
        raise SystemExit(f"an application is running on the console ({tid}); close it or pass --restart")
    if running:
        with botbase.connect(cfg) as b:
            print("killGame:", b.kill_game())
            time.sleep(3)
            b.click("A")  # dismisses "The software was closed because an error occurred"
            time.sleep(2)
    with sdfiles.open_sd(cfg, a.transport) as sd:
        print("transport:", sd.kind)
        cur = remote_current(sd, cfg)
        if cur.get("note", "").startswith("installed by hand"):
            # no manifest yet: hash the live files whose size matches, so an unchanged 50+ MB cache is not re-sent
            for rel, f in m["files"].items():
                live = cur["files"].get(rel) or {}
                if live.get("size") == f["size"]:
                    live["sha256"] = sd.sha256(posixpath.join(cfg["nro_dir"], rel))
        backup_slot = cur.get("slot") if cur.get("slot") in ("known_good", "failed") else "experimental"
        backup_dir = posixpath.join(cfg["nro_dir"], "deploy", backup_slot, cur["build_id"])
        sd.mkdirs(posixpath.join(cfg["nro_dir"], "deploy"))
        changed = []
        for rel, f in m["files"].items():
            old = cur.get("files", {}).get(rel)
            same = old and old.get("sha256") == f["sha256"] and old.get("size") == f["size"]
            if a.full or not same:
                changed.append(rel)
        print(f"currently installed: {cur['build_id']} ({cur.get('slot')}); files to upload: {len(changed)} of "
              f"{len(m['files'])}")
        for rel in changed:
            live = posixpath.join(cfg["nro_dir"], rel)
            if sd.exists(live):
                dst = posixpath.join(backup_dir, rel)
                sd.mkdirs(posixpath.dirname(dst))
                if sd.exists(dst):
                    sd.delete(dst)
                sd.rename(live, dst)
                print(f"  backed up {live} -> {dst}")
        for rel in changed:
            live = posixpath.join(cfg["nro_dir"], rel)
            sd.mkdirs(posixpath.dirname(live))
            data = open(files[rel], "rb").read()
            t = time.time()
            sd.put(live, data, verify=not a.size_only,
                   progress=lambda d, n: print(f"\r  {rel}: {d >> 20}/{n >> 20} MiB", end=""))
            print(f"\r  uploaded {rel}: {len(data)} bytes in {time.time() - t:.1f} s, "
                  f"{'size' if a.size_only else 'size + SHA-256'} verified")
        cur_doc = dict(m)
        cur_doc["deployed"] = datetime.datetime.now().isoformat(timespec="seconds")
        cur_doc["previous"] = {"build_id": cur["build_id"], "backup": backup_dir}
        cur_doc["files"] = {k: {"sha256": v["sha256"], "size": v["size"]} for k, v in m["files"].items()}
        sd.put(posixpath.join(cfg["nro_dir"], "deploy", "current.json"), json.dumps(cur_doc, indent=2).encode())
        append_history(sd, cfg, f"{cur_doc['deployed']} {m['build_id']} {a.slot} uploaded={','.join(changed)} "
                                 f"previous={cur['build_id']}->{backup_slot}")
        st = load_state(cfg)
        if cur["build_id"] and backup_slot == "experimental":
            st["experimental"].append(cur["build_id"])
        st["current"] = {"build_id": m["build_id"], "slot": a.slot, "local": local, "deployed": cur_doc["deployed"]}
        st["history"].append({"build_id": m["build_id"], "deployed": cur_doc["deployed"], "uploaded": changed})
        if backup_slot == "known_good":
            st["known_good"] = {"build_id": cur["build_id"], "backup": backup_dir}
        save_state(cfg, st)
        print(f"deployed {m['build_id']}")
        if a.restart:
            before = newest_log_number(sd, cfg)
            n = launch(cfg, sd, before)
            if n is None:
                print("FAILED TO VERIFY START: no new renut log appeared; run 'deploy.py mark-failed' if it crashed")
                return 3
            print(f"started: renut_{n:03d}.log is being written")
            if a.collect:
                os.system(f'"{sys.executable}" "{os.path.join(TOOLS, "switch_logs", "collect.py")}" --session start')
    return 0


def _set_slot(a, cfg, slot):
    with sdfiles.open_sd(cfg, a.transport) as sd:
        p = posixpath.join(cfg["nro_dir"], "deploy", "current.json")
        cur = json.loads(sd.read(p).decode())
        cur["slot"] = slot
        sd.put(p, json.dumps(cur, indent=2).encode())
        append_history(sd, cfg, f"{datetime.datetime.now().isoformat(timespec='seconds')} {cur['build_id']} marked {slot}")
    st = load_state(cfg)
    if slot == "known_good":
        st["known_good"] = {"build_id": cur["build_id"], "backup": "(live)"}
    else:
        st["failed"].append(cur["build_id"])
    if st.get("current"):
        st["current"]["slot"] = slot
    save_state(cfg, st)
    # move the PC copy into the matching folder
    for s in SLOTS:
        src = os.path.join(cfg["local_builds_dir"], s, cur["build_id"])
        if os.path.isdir(src) and s != slot:
            dst = os.path.join(cfg["local_builds_dir"], slot, cur["build_id"])
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            if not os.path.exists(dst):
                shutil.move(src, dst)
    print(f"{cur['build_id']} is now {slot}")
    return 0


def cmd_rollback(a, cfg):
    with sdfiles.open_sd(cfg, a.transport) as sd:
        cur = remote_current(sd, cfg)
        good_root = posixpath.join(cfg["nro_dir"], "deploy", "known_good")
        builds = sorted(e["name"] for e in sd.list(good_root) if e["type"] == "dir")
        if not builds:
            raise SystemExit("no known-good build backed up on the SD card")
        good = a.build or builds[-1]
        src = posixpath.join(good_root, good)
        moved_to = posixpath.join(cfg["nro_dir"], "deploy", "failed" if cur.get("slot") == "failed" else "experimental",
                                  cur["build_id"])

        def walk(d, rel=""):
            for e in sd.list(d):
                r = posixpath.join(rel, e["name"]) if rel else e["name"]
                if e["type"] == "dir":
                    yield from walk(posixpath.join(d, e["name"]), r)
                else:
                    yield r
        for rel in list(walk(src)):
            live = posixpath.join(cfg["nro_dir"], rel)
            if sd.exists(live):
                dst = posixpath.join(moved_to, rel)
                sd.mkdirs(posixpath.dirname(dst))
                sd.rename(live, dst)
            sd.rename(posixpath.join(src, rel), live)
            print(f"  restored {rel}")
        doc = {"build_id": good, "slot": "known_good", "files": {}, "deployed": datetime.datetime.now().isoformat(),
               "note": f"rollback from {cur['build_id']}"}
        sd.put(posixpath.join(cfg["nro_dir"], "deploy", "current.json"), json.dumps(doc, indent=2).encode())
        append_history(sd, cfg, f"{doc['deployed']} rollback {cur['build_id']} -> {good}")
    print(f"rolled back to {good}; files not in its backup stayed as they were (shared files)")
    return 0


def cmd_status(a, cfg):
    st = load_state(cfg)
    print("PC state:", json.dumps({k: st.get(k) for k in ("current", "known_good")}, indent=2))
    with sdfiles.open_sd(cfg, a.transport) as sd:
        cur = remote_current(sd, cfg)
        print("console:", json.dumps({k: cur.get(k) for k in ("build_id", "slot", "deployed", "note")}, indent=2))
        root = posixpath.join(cfg["nro_dir"], "deploy")
        for s in ("known_good", "experimental", "failed"):
            try:
                names = [e["name"] for e in sd.list(posixpath.join(root, s)) if e["type"] == "dir"]
            except Exception:  # noqa: BLE001
                names = []
            print(f"  SD deploy/{s}: {', '.join(names) or '-'}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    switchcfg.add_common_args(ap)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("deploy")
    p.add_argument("--package")
    p.add_argument("--nro")
    p.add_argument("--elf")
    p.add_argument("--toml")
    p.add_argument("--cache")
    p.add_argument("--glsl", help="deko3d_glsl.bin (the complete GLSL package beside the NRO)")
    p.add_argument("--name", help="label, e.g. rc5 or k160")
    p.add_argument("--slot", choices=["current", "experimental"], default="current")
    p.add_argument("--full", action="store_true", help="upload every file, not only the changed ones")
    p.add_argument("--size-only", action="store_true", help="skip the SHA-256 read-back (faster on big caches)")
    p.add_argument("--restart", action="store_true", help="close the running game first, launch after")
    p.add_argument("--collect", action="store_true", help="collect the first logs after the start")
    p.add_argument("--dry-run", action="store_true", help="hash and archive on the PC only")
    sub.add_parser("status")
    sub.add_parser("mark-good")
    sub.add_parser("mark-failed")
    p = sub.add_parser("rollback")
    p.add_argument("--build")
    a = ap.parse_args()
    cfg = switchcfg.from_args(a, need_ip=not getattr(a, "dry_run", False))
    if a.cmd == "deploy":
        return cmd_deploy(a, cfg)
    if a.cmd == "status":
        return cmd_status(a, cfg)
    if a.cmd == "mark-good":
        return _set_slot(a, cfg, "known_good")
    if a.cmd == "mark-failed":
        return _set_slot(a, cfg, "failed")
    return cmd_rollback(a, cfg)


if __name__ == "__main__":
    sys.exit(main())
