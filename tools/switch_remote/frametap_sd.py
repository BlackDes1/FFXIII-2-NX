#!/usr/bin/env python3
"""ffxiii2-nx: Switch Frame Tap (github.com/TomasUribe/switch-frame-tap, GPL-2.0) on the console's SD card, over the
existing sys-botbase / FTP file transport. Nothing here reboots the console or touches NAND.

  python tools/switch_remote/frametap_sd.py inspect
      what is installed: every atmosphere/contents/<id> (name from toolbox.json, boot2 flag, mitm.lst), the overlays,
      known conflicts (SysDVR owns the USB gadget; another vi/dispdrv/audout/appletOE mitm), NAND-backup folders
      hekate / NxNandManager leave on the card, and whether Frame Tap is already there
  python tools/switch_remote/frametap_sd.py backup [--out <Works>/switch_sd_backup]
      copies /config, /atmosphere/config, /atmosphere/hosts, /atmosphere/contents/*/toolbox.json + flags + mitm.lst,
      /switch/.overlays listing and /bootloader/*.ini to <out>/<YYYYmmdd_HHMMSS>/ with manifest.json (size, SHA-256)
  python tools/switch_remote/frametap_sd.py install <switch-frame-tap-X-switch.zip> --sha256 <hex> [--replace]
      writes the ZIP's files (only atmosphere/contents/0100000000000C2[01]/, switch/switch-frame-tap/,
      switch/.overlays/switch-frame-tap.ovl, config/switch-frame-tap/) - each one verified by SHA-256 read-back.
      A file that already exists and differs is kept unless --replace (an update); an existing config.ini is never
      in the ZIP and never written. Run `backup` first.
  python tools/switch_remote/frametap_sd.py config [--set key=value ...]
      prints sdmc:/config/switch-frame-tap/config.ini; with --set, copies it to config.ini.<time>.bak beside it first
      and then changes only those keys (a key not in the file is appended)
  python tools/switch_remote/frametap_sd.py logs [--out <dir>]
      fetches log.txt, log.prev.txt, last.txt, launcher.txt and the newest crash reports
  python tools/switch_remote/frametap_sd.py shots --out <dir> [--since <name>]
      copies the native-resolution PNG screenshots from /switch/switch-frame-tap/screenshots
"""
import argparse
import datetime
import hashlib
import io
import json
import os
import posixpath
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from switch_remote import sdfiles, switchcfg  # noqa: E402

SFT_IDS = ("0100000000000C20", "0100000000000C21")
SFT_PREFIXES = ("atmosphere/contents/0100000000000C20/", "atmosphere/contents/0100000000000C21/",
                "switch/switch-frame-tap/", "switch/.overlays/switch-frame-tap.ovl", "config/switch-frame-tap/")
CONF_DIR = "/config/switch-frame-tap"
CONF = CONF_DIR + "/config.ini"
SHOTS = "/switch/switch-frame-tap/screenshots"
KNOWN = {
    "00FF0000A53BB665": "SysDVR (owns the USB gadget: conflicts with Frame Tap over USB)",
    "00FF0000636C6BFF": "sys-clk",
    "420000000007E51A": "nx-ovlloader (Tesla / Ultrahand overlays)",
    "430000000000000B": "sys-botbase",
    "420000000000000E": "sys-ftpd",
    "0100000000000C20": "Switch Frame Tap: applet-mitm (the capture module)",
    "0100000000000C21": "Switch Frame Tap: launcher",
    "0100000000000352": "emuiibo",
    "690000000000000D": "sys-con",
    "4200000000000010": "ldn_mitm",
    "0100000000000FAF": "?",
}
MITM_WATCH = ("vi:u", "vi:m", "vi:s", "dispdrv", "audout:u", "appletOE", "nvdrv:s", "lbl")


_FTP = {}


def rd(sd, path):
    """sys-botbase file commands cannot take spaces in a path: read such a file over FTP instead."""
    if " " not in path or sd.kind == "ftp":
        return sd.read(path)
    if "ftp" not in _FTP:
        _FTP["ftp"] = sdfiles.FtpSd(_FTP["cfg"])
    return _FTP["ftp"].read(path)


def lsx(sd, path):
    if " " not in path or sd.kind == "ftp":
        return ls(sd, path)
    if "ftp" not in _FTP:
        _FTP["ftp"] = sdfiles.FtpSd(_FTP["cfg"])
    return ls(_FTP["ftp"], path)


def now():
    return datetime.datetime.now().strftime("%Y%m%d_%H%M%S")


def ls(sd, path):
    try:
        return sd.list(path)
    except Exception:  # noqa: BLE001 - a missing folder lists as empty
        return None


def read_text(sd, path):
    try:
        return sd.read(path).decode("utf-8", "replace")
    except Exception:  # noqa: BLE001
        return None


def walk(sd, root, rel=""):
    for e in lsx(sd, posixpath.join(root, rel) if rel else root) or []:
        r = posixpath.join(rel, e["name"]) if rel else e["name"]
        if e["type"] == "dir":
            yield from walk(sd, root, r)
        else:
            yield r, e["size"]


def cmd_inspect(sd, a):
    rep = {"contents": [], "overlays": [], "conflicts": [], "nand_backup_folders": [], "frame_tap": {}}
    for e in ls(sd, "/atmosphere/contents") or []:
        if e["type"] != "dir":
            continue
        tid = e["name"].upper()
        base = "/atmosphere/contents/" + e["name"]
        names = {x["name"] for x in ls(sd, base) or []}
        flags = {x["name"] for x in ls(sd, base + "/flags") or []} if "flags" in names else set()
        item = {"id": tid, "known": KNOWN.get(tid, ""), "files": sorted(names), "boot2": "boot2.flag" in flags}
        if "toolbox.json" in names:
            t = read_text(sd, base + "/toolbox.json")
            try:
                item["toolbox_name"] = json.loads(t).get("name")
            except Exception:  # noqa: BLE001
                item["toolbox_name"] = (t or "")[:60]
        if "mitm.lst" in names:
            item["mitm"] = [x.strip() for x in (read_text(sd, base + "/mitm.lst") or "").splitlines() if x.strip()]
        rep["contents"].append(item)
        if tid == "00FF0000A53BB665" and item["boot2"]:
            rep["conflicts"].append("SysDVR starts at boot: it owns the USB gadget, Frame Tap's USB mode cannot")
        clash = [m for m in item.get("mitm", []) if m in MITM_WATCH and tid not in SFT_IDS]
        if clash:
            rep["conflicts"].append(f"{tid} ({item.get('toolbox_name') or item['known']}) also intercepts {clash}")
    rep["overlays"] = [x["name"] for x in ls(sd, "/switch/.overlays") or []]
    for p in ("/backup", "/emuMMC", "/emummc", "/NxNandManager", "/switch/NxNandManager"):
        x = ls(sd, p)
        if x is not None:
            rep["nand_backup_folders"].append({p: [y["name"] for y in x][:20]})
    rep["frame_tap"] = {
        "sysmodules": [i for i in SFT_IDS if any(c["id"] == i for c in rep["contents"])],
        "manager_nro": sd.exists("/switch/switch-frame-tap/switch-frame-tap.nro"),
        "config_ini": read_text(sd, CONF) is not None,
        "logs": [x["name"] for x in ls(sd, CONF_DIR) or [] if x["type"] != "dir"],
    }
    print(json.dumps(rep, indent=1))
    return 0


def save_tree(sd, out, sd_root, manifest):
    for rel, size in walk(sd, sd_root):
        path = posixpath.join(sd_root, rel)
        data = rd(sd, path)
        dst = os.path.join(out, *path.strip("/").split("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as f:
            f.write(data)
        manifest.append({"path": path, "size": len(data), "listed": size, "sha256": hashlib.sha256(data).hexdigest()})


def cmd_backup(sd, a):
    out = os.path.join(a.out or os.path.join(switchcfg.WORKS, "switch_sd_backup"), now())
    os.makedirs(out, exist_ok=True)
    man = []
    for root in ("/config", "/atmosphere/config", "/atmosphere/hosts", "/bootloader/ini"):
        save_tree(sd, out, root, man)
    for f in ("/emuMMC/emummc.ini", "/bootloader/hekate_ipl.ini", "/atmosphere/system_settings.ini", "/atmosphere/config_templates/override_config.ini"):
        try:
            data = sd.read(f)
        except Exception:  # noqa: BLE001
            continue
        dst = os.path.join(out, *f.strip("/").split("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        open(dst, "wb").write(data)
        man.append({"path": f, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    listing = {}
    for e in ls(sd, "/atmosphere/contents") or []:
        base = "/atmosphere/contents/" + e["name"]
        top = ls(sd, base) or []
        listing[e["name"]] = [(x["name"] + ("/" if x["type"] == "dir" else ""), x.get("size")) for x in top]
        names = [x["name"] for x in top if x["type"] != "dir"]
        if any(x["name"] == "flags" and x["type"] == "dir" for x in top):
            names += ["flags/" + x["name"] for x in ls(sd, base + "/flags") or []]
        for r in names:
            if r in ("toolbox.json", "mitm.lst") or r.startswith("flags/"):
                p = base + "/" + r
                data = sd.read(p)
                dst = os.path.join(out, *p.strip("/").split("/"))
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                open(dst, "wb").write(data)
                man.append({"path": p, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    listing["/switch/.overlays"] = [(x["name"], x.get("size")) for x in ls(sd, "/switch/.overlays") or []]
    json.dump({"files": man, "contents_listing": listing}, open(os.path.join(out, "manifest.json"), "w"), indent=1)
    print(f"{len(man)} files backed up to {out}")
    return 0


def cmd_install(sd, a):
    raw = open(a.zip, "rb").read()
    h = hashlib.sha256(raw).hexdigest()
    if h.lower() != a.sha256.lower():
        print(f"SHA-256 {h} is not the expected {a.sha256}: nothing installed")
        return 2
    z = zipfile.ZipFile(io.BytesIO(raw))
    files = [i for i in z.infolist() if not i.is_dir()]
    bad = [i.filename for i in files if not i.filename.startswith(SFT_PREFIXES)]
    if bad:
        print("unexpected files in the ZIP, nothing installed:", bad)
        return 2
    wrote = kept = same = 0
    for i in files:
        path = "/" + i.filename
        data = z.read(i)
        if path.endswith("/config.ini"):
            print("skip (never overwrite settings):", path)
            continue
        cur = None
        if sd.exists(path):
            cur = sd.read(path)
        if cur == data:
            same += 1
            continue
        if cur is not None and not a.replace:
            print("kept (exists and differs; --replace to update):", path)
            kept += 1
            continue
        sd.mkdirs(posixpath.dirname(path))
        sd.put(path, data, verify=True)
        wrote += 1
        print(f"wrote {path} ({len(data)} bytes, SHA-256 verified)")
    print(f"done: {wrote} written, {same} already identical, {kept} kept. Reboot the console to start the sysmodule.")
    return 0


def cmd_config(sd, a):
    text = read_text(sd, CONF)
    if text is None:
        print(f"{CONF} does not exist yet (the manager writes it on its first save)")
        if not a.set:
            return 1
        text = ""
    if not a.set:
        print(text)
        return 0
    if text:
        bak = f"{CONF}.{now()}.bak"
        sd.put(bak, text.encode("utf-8"), verify=True)
        print("backup:", bak)
    lines = text.splitlines()
    for kv in a.set:
        k, v = kv.split("=", 1)
        k, v = k.strip(), v.strip()
        for n, ln in enumerate(lines):
            if ln.split("=", 1)[0].strip() == k and not ln.lstrip().startswith("#"):
                lines[n] = f"{k} = {v}"
                break
        else:
            lines.append(f"{k} = {v}")
        print(f"{k} = {v}")
    sd.put(CONF, ("\n".join(lines) + "\n").encode("utf-8"), verify=True)
    return 0


def cmd_logs(sd, a):
    out = a.out or os.path.join(switchcfg.WORKS, "ffxiii2-k", "logs", "frametap", now())
    os.makedirs(out, exist_ok=True)
    for n in ("log.txt", "log.prev.txt", "last.txt", "launcher.txt"):
        try:
            data = sd.read(CONF_DIR + "/" + n)
        except Exception:  # noqa: BLE001
            print("missing:", n)
            continue
        open(os.path.join(out, n), "wb").write(data)
        print(f"{n}: {len(data)} bytes")
    crashes = sorted(x["name"] for x in ls(sd, "/atmosphere/crash_reports") or [] if x["type"] != "dir")
    for n in (crashes[-a.crashes:] if a.crashes > 0 else []):
        open(os.path.join(out, "crash_" + n), "wb").write(sd.read("/atmosphere/crash_reports/" + n))
        print("crash report:", n)
    print("->", out)
    return 0


def cmd_shots(sd, a):
    os.makedirs(a.out, exist_ok=True)
    names = sorted(x["name"] for x in ls(sd, SHOTS) or [] if x["type"] != "dir")
    if a.since:
        names = [n for n in names if n > a.since]
    for n in names:
        dst = os.path.join(a.out, n)
        if os.path.exists(dst):
            continue
        open(dst, "wb").write(sd.read(SHOTS + "/" + n))
        print(n)
    print(f"{len(names)} screenshots -> {a.out}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    switchcfg.add_common_args(ap)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("inspect")
    p = sub.add_parser("backup")
    p.add_argument("--out")
    p = sub.add_parser("install")
    p.add_argument("zip")
    p.add_argument("--sha256", required=True)
    p.add_argument("--replace", action="store_true")
    p = sub.add_parser("config")
    p.add_argument("--set", nargs="*")
    p = sub.add_parser("logs")
    p.add_argument("--out")
    p.add_argument("--crashes", type=int, default=3)
    p = sub.add_parser("shots")
    p.add_argument("--out", required=True)
    p.add_argument("--since")
    a = ap.parse_args()
    cfg = switchcfg.from_args(a)
    _FTP["cfg"] = cfg
    with sdfiles.open_sd(cfg, a.transport) as sd:
        return {"inspect": cmd_inspect, "backup": cmd_backup, "install": cmd_install, "config": cmd_config,
                "logs": cmd_logs, "shots": cmd_shots}[a.cmd](sd, a)


if __name__ == "__main__":
    sys.exit(main())
