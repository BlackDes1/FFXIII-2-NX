#!/usr/bin/env python3
"""ffxiii2-nx: copy the game's save data from the console to the PC before a test plays on (FF XIII-2 autosaves).

  python tools/switch_remote/backup_saves.py [--remote /switch/ffxiii2-nx/renut] [--out <Works>/switch_saves_backup]

Every file under the remote folder is read (sys-botbase fileRead), checked against its listed size, and written to
<out>/<YYYYmmdd_HHMMSS>/ with the same relative paths, plus manifest.json (path, size, SHA-256). Nothing on the console
is changed. Restore is a manual decision (switch.py put); this tool never writes saves back.
"""
import argparse
import datetime
import hashlib
import json
import os
import posixpath
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from switch_remote import sdfiles, switchcfg  # noqa: E402


def walk(sd, root, rel=""):
    for e in sd.list(posixpath.join(root, rel) if rel else root):
        r = posixpath.join(rel, e["name"]) if rel else e["name"]
        if e["type"] == "dir":
            yield from walk(sd, root, r)
        else:
            yield r, e["size"]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    switchcfg.add_common_args(ap)
    ap.add_argument("--remote", default=None, help="default: <nro_dir>/renut")
    ap.add_argument("--out", default=None)
    a = ap.parse_args()
    cfg = switchcfg.from_args(a)
    remote = a.remote or posixpath.join(cfg["nro_dir"], "renut")
    out = os.path.join(a.out or os.path.join(switchcfg.WORKS, "switch_saves_backup"),
                       datetime.datetime.now().strftime("%Y%m%d_%H%M%S"))
    manifest = []
    with sdfiles.open_sd(cfg, a.transport) as sd:
        for rel, size in walk(sd, remote):
            data = sd.read(posixpath.join(remote, rel))
            if size is not None and size >= 0 and len(data) != size:
                raise SystemExit(f"{rel}: read {len(data)} of {size} bytes")
            dst = os.path.join(out, rel.replace("/", os.sep))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, "wb") as f:
                f.write(data)
            manifest.append({"path": rel, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
            print(f"  {len(data):>9}  {rel}")
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump({"remote": remote, "files": manifest}, f, indent=1)
    print(f"{len(manifest)} files, {sum(m['size'] for m in manifest)} bytes -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
