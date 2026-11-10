#!/usr/bin/env python3
"""ffxiii2-nx (2026-11-07): compare two save backups made by backup_saves.py (their manifest.json files).

  python tools/switch_remote/compare_save_backups.py <older backup dir> <newer backup dir>

Prints every file that was added, removed or changed (SHA-256), grouped by save slot (ff13-NN.dat). Reads only.
"""
import json
import os
import sys


def load(d):
    with open(os.path.join(d, "manifest.json"), encoding="utf-8") as f:
        m = json.load(f)
    files = m.get("files", m)
    out = {}
    if isinstance(files, dict):
        for k, v in files.items():
            out[k] = v.get("sha256") if isinstance(v, dict) else v
    else:
        for e in files:
            out[e["path"]] = e.get("sha256")
    return out


def main():
    a, b = load(sys.argv[1]), load(sys.argv[2])
    added = sorted(set(b) - set(a))
    removed = sorted(set(a) - set(b))
    changed = sorted(k for k in set(a) & set(b) if a[k] != b[k])
    print(f"older {len(a)} files, newer {len(b)} files: {len(added)} added, {len(removed)} removed, {len(changed)} changed")
    for name, lst in (("added", added), ("removed", removed), ("changed", changed)):
        for k in lst:
            print(f"  {name:8s} {k}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
