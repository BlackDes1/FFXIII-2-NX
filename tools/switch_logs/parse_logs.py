#!/usr/bin/env python3
"""ffxiii2-nx: what the console's logs say, in one page: errors, warnings (repeats folded), crashes, performance.

  python tools/switch_logs/parse_logs.py <log files or a session folder> [--json out.json]

Knows the files the Switch build writes (investigated 2026-11-05, see docs/LIVE_SWITCH_TESTING.md, 10):
  logs/renut_NNN.log    spdlog of the run (one file per boot, NNN counts up). "[info]/[warning]/[error]/[critical]",
                        "[renut] T s: N game frames (F fps), render thread busy R ms a frame (..), GPU G ms a frame",
                        "[audiodiag]", "[xmak]", "uam N compiles", "not in the shader cache", "run-time variant".
                        Written by a writer thread at least once a second (sdk/src/core/logging.cpp), so it can be
                        read while the game runs; the last second may be missing.
  logs/ffxiii2_NNN.log  the game's own text output.
  logs/rex/rex_profile.log  10 s reports of switch_perf.cpp ("==== T s | mode | game F fps | CPU total C% ..."),
                        thread CPU and, with logs/rex/perfil_pilas.flag, stack samples ("image+0x..").
  logs/rex/rex_crash.log    abort()/exception reports (stack as image+0x..) -> crash.
  logs/rex/rex_stderr.log   stderr, unbuffered.
"""
import argparse
import collections
import datetime
import json
import os
import re
import sys

RE_LEVEL = re.compile(r"^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+)\] \[(\w+)\] \[(\w+)\] (?:\[t\d+\] )?(.*)$")
RE_PERIOD = re.compile(r"\[renut\] ([0-9.]+) s: (\d+) game frames \(([0-9.]+) fps\), render thread busy ([0-9.]+) ms a "
                       r"frame \((\d+) frames\), GPU ([0-9.]+) ms a frame")
RE_PROFILE = re.compile(r"^==== ([0-9.]+) s \| mode: ([^|]+)\| game ([0-9.]+) fps \| CPU total (\d+)%")
RE_DRAWS = re.compile(r"per frame: (\d+) draws")
RE_UAM = re.compile(r"uam (\d+) compiles \(avg (\d+) ms, (\d+) failed\)")
RE_NUM = re.compile(r"0x[0-9a-fA-F]+|\b\d+(\.\d+)?\b")
CRASH_WORDS = ("abort", "fatal", "exception", "crash", "panic", "assert", "segfault", "data abort", "2345-0101")
STALL_FPS = 5.0     # a 10 s period below this is flagged
HITCH_MS = 45.0     # a render-thread frame above this is flagged


def norm(msg):
    return RE_NUM.sub("#", msg)[:160]


def parse_renut(path, out):
    levels = collections.Counter()
    warns = collections.Counter()
    errors = collections.Counter()
    first = {}
    perf = []
    lines = 0
    t0 = None
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            lines += 1
            m = RE_LEVEL.match(line.rstrip("\n"))
            if not m:
                continue
            ts, level, _cat, msg = m.groups()
            try:
                now = datetime.datetime.strptime(ts, "%Y-%m-%d %H:%M:%S.%f")
            except ValueError:
                now = None
            if t0 is None and now is not None:
                t0 = now
            levels[level] += 1
            if level in ("warning", "warn"):
                warns[norm(msg)] += 1
                first.setdefault(norm(msg), msg)
            elif level in ("error", "err", "critical"):
                errors[norm(msg)] += 1
                first.setdefault(norm(msg), msg)
            p = RE_PERIOD.search(msg)
            if p:
                # "[renut] 10.0 s: ..." gives the period's length; t is the time since the log's first line
                perf.append({"t": (now - t0).total_seconds() if now and t0 else 0.0, "window_s": float(p.group(1)),
                             "fps": float(p.group(3)), "render_ms": float(p.group(4)), "gpu_ms": float(p.group(6)),
                             "time": ts})
            u = RE_UAM.search(msg)
            if u:
                out["shader_compiles"].append({"file": os.path.basename(path), "compiles": int(u.group(1)),
                                               "avg_ms": int(u.group(2)), "failed": int(u.group(3))})
            if "not in the shader cache" in msg or "run-time variant" in msg:
                out["runtime_shader_lines"] += 1
            if any(w in msg.lower() for w in CRASH_WORDS) and level in ("error", "critical"):
                out["crash_hints"].append(f"{os.path.basename(path)}: {msg[:200]}")
    out["files"].append({"file": path, "kind": "renut", "lines": lines, "levels": dict(levels)})
    out["warnings"] += [{"file": os.path.basename(path), "count": n, "example": first[k]} for k, n in warns.most_common()]
    out["errors"] += [{"file": os.path.basename(path), "count": n, "example": first[k]} for k, n in errors.most_common()]
    for p in perf:
        p["file"] = os.path.basename(path)
        if p["fps"] < STALL_FPS and p["t"] > 30:
            out["anomalies"].append(f"{p['file']} at {p['t']:.0f} s: {p['fps']:.1f} fps (stall)")
        if p["render_ms"] > HITCH_MS:
            out["anomalies"].append(f"{p['file']} at {p['t']:.0f} s: render thread {p['render_ms']:.0f} ms a frame")
    out["perf"] += perf


def parse_profile(path, out):
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    periods = []
    for m in re.finditer(r"^==== .*$", text, re.M):
        h = RE_PROFILE.match(m.group(0))
        if h:
            seg = text[m.end():m.end() + 4000]
            d = RE_DRAWS.search(seg)
            periods.append({"t": float(h.group(1)), "mode": h.group(2).strip(), "fps": float(h.group(3)),
                            "cpu_total": int(h.group(4)), "draws_per_frame": int(d.group(1)) if d else None})
    sampling = "active" if "Stack sampling: active" in text else "disabled"
    hitches = re.findall(r"== during the hitches: (\d+) frames over 45 ms \(([0-9.]+) ms in total\)", text)
    out["profile"] = {"file": path, "sampling": sampling, "periods": periods,
                      "hitch_reports": [{"frames": int(a), "ms": float(b)} for a, b in hitches]}
    out["files"].append({"file": path, "kind": "rex_profile", "periods": len(periods)})


def parse_crash(path, out):
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    if text.strip():
        out["crash_hints"].append(f"{os.path.basename(path)}: {len(text.splitlines())} lines (crash report present; "
                                  "symbolize with tools/switch_profiling/symbolize.py)")
    out["files"].append({"file": path, "kind": "crash", "bytes": len(text)})


def analyse(paths):
    out = {"files": [], "errors": [], "warnings": [], "crash_hints": [], "anomalies": [], "perf": [],
           "shader_compiles": [], "runtime_shader_lines": 0, "profile": None}
    files = []
    for p in paths:
        if os.path.isdir(p):
            for d, _, names in os.walk(p):
                files += [os.path.join(d, n) for n in names if n.endswith(".log")]
        else:
            files.append(p)
    for f in sorted(files):
        n = os.path.basename(f)
        if n.startswith("renut_") or n.startswith("ffxiii2_"):
            parse_renut(f, out)
        elif n.startswith("rex_profile"):
            parse_profile(f, out)
        elif n.startswith("rex_crash"):
            parse_crash(f, out)
    if out["perf"]:
        fps = [p["fps"] for p in out["perf"]]
        out["perf_summary"] = {"periods": len(fps), "fps_min": min(fps), "fps_max": max(fps),
                               "fps_mean": round(sum(fps) / len(fps), 2),
                               "render_ms_max": max(p["render_ms"] for p in out["perf"])}
    return out


def report(out):
    lines = []
    for f in out["files"]:
        lines.append(f"file {f['file']}  " + ", ".join(f"{k} {v}" for k, v in f.items() if k not in ("file",)))
    if out["crash_hints"]:
        lines.append("\n!! CRASH / FATAL")
        lines += ["   " + c for c in out["crash_hints"]]
    if out["errors"]:
        lines.append(f"\n!! ERRORS ({sum(e['count'] for e in out['errors'])} lines, {len(out['errors'])} kinds)")
        lines += [f"   {e['count']:5d}x {e['file']}: {e['example'][:170]}" for e in out["errors"][:25]]
    if out["warnings"]:
        lines.append(f"\n!  WARNINGS ({sum(w['count'] for w in out['warnings'])} lines, {len(out['warnings'])} kinds; "
                     "repeated ones first)")
        lines += [f"   {w['count']:5d}x {w['file']}: {w['example'][:170]}" for w in out["warnings"][:25]]
    if out.get("perf_summary"):
        s = out["perf_summary"]
        lines.append(f"\nPERFORMANCE (renut 10 s periods): {s['periods']} periods, fps min {s['fps_min']} / mean "
                     f"{s['fps_mean']} / max {s['fps_max']}, render thread up to {s['render_ms_max']} ms a frame")
        lines.append("   fps: " + " ".join(f"{p['fps']:.1f}" for p in out["perf"][:60]))
    if out["anomalies"]:
        lines.append("\n!  PERFORMANCE ANOMALIES")
        lines += ["   " + a for a in out["anomalies"][:40]]
    if out["profile"]:
        p = out["profile"]
        lines.append(f"\nrex_profile.log: stack sampling {p['sampling']}, {len(p['periods'])} reports"
                     + (" (sampler overhead: these fps are NOT benchmarks)" if p["sampling"] == "active" else ""))
        lines.append("   fps: " + " ".join(f"{q['fps']:.1f}" for q in p["periods"][:60]))
        for h in p["hitch_reports"][:5]:
            lines.append(f"   hitch report: {h['frames']} frames over 45 ms, {h['ms']:.0f} ms")
    if out["shader_compiles"] or out["runtime_shader_lines"]:
        tot = sum(s["compiles"] for s in out["shader_compiles"])
        lines.append(f"\nSHADERS: run-time uam compiles reported {tot}; 'not in cache'/'run-time variant' lines "
                     f"{out['runtime_shader_lines']}")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--json")
    a = ap.parse_args()
    out = analyse(a.paths)
    print(report(out))
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump(out, f, indent=2)
    return 1 if out["crash_hints"] else 0


if __name__ == "__main__":
    sys.exit(main())
