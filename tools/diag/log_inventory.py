#!/usr/bin/env python3
"""ffxiii2-nx: inventory of every diagnostic file under Works (logs, crash reports, profiles, JSON/JSONL/CSV reports).

  python tools/diag/log_inventory.py [--root <Works>] [--out docs/diagnostics] [--max-read-mb 64]

For each file: path, kind, size, modification time, the build / session it belongs to (from its folder: switch log
sessions are logs/switch/<YYYYmmdd_HHMM>_<nro hash>/<session>/, run folders out/<kind>_runs/<run>/), whether it was read
whole, and for text logs the counts of error / warning / fatal lines and the most frequent distinct messages (numbers,
addresses and hashes folded so repeats group). Writes LOG_INVENTORY.json (everything) and LOG_INVENTORY.md (per folder
group, then the messages seen across many files). Read-only: nothing is changed or deleted.
"""
import argparse
import collections
import datetime
import json
import os
import re
import sys

KINDS = {".log": "log", ".txt": "text", ".json": "json", ".jsonl": "jsonl", ".csv": "csv", ".dmp": "dump"}
SKIP_DIRS = {"thirdparty", "node_modules", ".git", "_to_delete", "generated", "CMakeFiles", "__pycache__", "game_root",
             "game_data", "switch_sd_backup", "deko3d_shaders", "renut_shader_dump", "shader_capture", "deko3d_input"}
NAME_HINT = re.compile(r"(log|crash|fatal|profile|report|stderr|stdout|trace|perf|frametime|bench|timing|stats|"
                       r"build|session|manifest|launcher|last|diag)", re.I)
LEVEL = re.compile(r"\b(fatal|error|err|failed|failure|warning|warn|assert|abort|exception|panic|timeout|timed out)\b",
                   re.I)
FOLD = [(re.compile(r"\b0x[0-9a-fA-F]+\b"), "0x#"), (re.compile(r"\b[0-9a-fA-F]{8,}\b"), "#H"),
        (re.compile(r"\d+(\.\d+)?"), "#"), (re.compile(r"\s+"), " ")]
STAMP = re.compile(r"^\s*(\[[^\]]*\]\s*){1,4}")


def fold(line):
    s = STAMP.sub("", line.strip())
    for rx, rep in FOLD:
        s = rx.sub(rep, s)
    return s[:160]


def build_of(rel):
    parts = rel.replace("\\", "/").split("/")
    for i, p in enumerate(parts):
        if re.match(r"^\d{8}_\d{4}_[0-9a-f]{12}$", p):
            return p, parts[i + 1] if i + 1 < len(parts) - 1 else ""
        if p.endswith("_runs") and i + 1 < len(parts):
            return p, parts[i + 1]
        if p in ("visual", "frametap") and i + 1 < len(parts):
            return p + "/" + parts[i + 1], parts[i + 2] if i + 2 < len(parts) - 1 else ""
    return "", ""


def wanted(name):
    ext = os.path.splitext(name)[1].lower()
    if ext in (".log", ".jsonl", ".dmp"):
        return True
    if name.startswith("crash_") or name in ("last.txt", "launcher.txt"):
        return True
    return ext in (".txt", ".json", ".csv") and bool(NAME_HINT.search(name))


def scan(path, max_bytes):
    levels = collections.Counter()
    msgs = collections.Counter()
    lines = 0
    whole = True
    with open(path, "rb") as f:
        data = f.read(max_bytes + 1)
    if len(data) > max_bytes:
        data, whole = data[:max_bytes], False
    text = data.decode("utf-8", "replace").replace("\x00", "")
    for ln in text.splitlines():
        lines += 1
        m = LEVEL.search(ln)
        if m:
            lv = m.group(1).lower()
            lv = {"err": "error", "failed": "error", "failure": "error", "warn": "warning", "timed out": "timeout",
                  "exception": "error", "assert": "fatal", "abort": "fatal", "panic": "fatal"}.get(lv, lv)
            levels[lv] += 1
            msgs[fold(ln)] += 1
    return lines, whole, levels, msgs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.dirname(os.path.dirname(here))
    ap.add_argument("--root", default=os.path.dirname(repo))
    ap.add_argument("--out", default=os.path.join(repo, "docs", "diagnostics"))
    ap.add_argument("--max-read-mb", type=float, default=64)
    a = ap.parse_args()
    max_bytes = int(a.max_read_mb * (1 << 20))
    items = []
    across = collections.defaultdict(lambda: [0, 0, set()])  # message -> [lines, files, groups]
    for dirpath, dirnames, filenames in os.walk(a.root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for name in filenames:
            if not wanted(name):
                continue
            p = os.path.join(dirpath, name)
            rel = os.path.relpath(p, a.root)
            try:
                st = os.stat(p)
            except OSError:
                continue
            kind = "crash report" if name.startswith("crash_") else KINDS.get(os.path.splitext(name)[1].lower(), "other")
            build, session = build_of(rel)
            item = {"path": rel, "kind": kind, "bytes": st.st_size,
                    "mtime": datetime.datetime.fromtimestamp(st.st_mtime).isoformat(timespec="seconds"),
                    "build": build, "session": session}
            if kind in ("log", "text", "crash report", "jsonl", "csv", "json") and st.st_size:
                try:
                    lines, whole, levels, msgs = scan(p, max_bytes)
                except OSError as e:
                    item["error"] = str(e)
                else:
                    item.update(lines=lines, read_whole=whole, levels=dict(levels),
                                top=[[m, c] for m, c in msgs.most_common(8)])
                    group = os.path.dirname(rel).split(os.sep)[:3]
                    for m, c in msgs.items():
                        e = across[m]
                        e[0] += c
                        e[1] += 1
                        e[2].add("/".join(group))
            items.append(item)
    os.makedirs(a.out, exist_ok=True)
    json.dump({"made": datetime.datetime.now().isoformat(timespec="seconds"), "root": a.root, "files": items},
              open(os.path.join(a.out, "LOG_INVENTORY.json"), "w"), indent=1)
    groups = collections.defaultdict(list)
    for it in items:
        groups["/".join(it["path"].replace("\\", "/").split("/")[:3])].append(it)
    out = ["# Diagnostic file inventory", "",
           f"Made {datetime.datetime.now():%Y-%m-%d %H:%M} by `tools/diag/log_inventory.py` (read-only) over `{a.root}`. "
           f"{len(items)} files; the per-file detail (path, size, time, build, session, whole-read, levels, top "
           "messages) is in LOG_INVENTORY.json. Skipped folders: " + ", ".join(sorted(SKIP_DIRS)) + ".", "",
           "## By folder", "", "| folder | files | MB | newest | read whole | error | warning | fatal | timeout |",
           "|---|---|---|---|---|---|---|---|---|"]
    for g in sorted(groups):
        its = groups[g]
        lv = collections.Counter()
        for it in its:
            lv.update(it.get("levels", {}))
        out.append(f"| {g} | {len(its)} | {sum(i['bytes'] for i in its) / 1e6:.1f} | "
                   f"{max(i['mtime'] for i in its)[:16]} | {sum(1 for i in its if i.get('read_whole'))} | "
                   f"{lv['error']} | {lv['warning']} | {lv['fatal']} | {lv['timeout']} |")
    out += ["", "## Messages seen in the most files (numbers, addresses and hashes folded)", "",
            "| files | lines | folders | message |", "|---|---|---|---|"]
    for m, (n, f, gs) in sorted(across.items(), key=lambda kv: (-kv[1][1], -kv[1][0]))[:80]:
        out.append(f"| {f} | {n} | {len(gs)} | `{m.replace('|', '/')}` |")
    open(os.path.join(a.out, "LOG_INVENTORY.md"), "w", encoding="utf-8").write("\n".join(out) + "\n")
    print(f"{len(items)} files -> {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
