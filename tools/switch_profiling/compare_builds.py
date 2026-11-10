#!/usr/bin/env python3
"""ffxiii2-nx: build A vs build B from two test sessions (the evidence for keep-or-revert).

  python tools/switch_profiling/compare_builds.py <session A> <session B> [--min-gain 3]

Each session is a folder made by collect.py / run_scenario.py (logs/switch/<build_id>/<session>/). Compared:
renut 10 s periods (fps, render-thread ms, GPU ms; the first 30 s of a boot are left out), rex_profile fps and CPU,
error / warning counts, crashes, run-time shader compiles. The verdict needs the same scenario on both sides and a
mean fps difference larger than both the --min-gain percentage and the spread of the periods; otherwise it says
"no measurable difference". Sessions with the stack sampler on are refused for fps (its overhead).
"""
import argparse
import json
import os
import statistics
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "switch_logs"))
import parse_logs  # noqa: E402


def load(d):
    meta = {}
    for name in ("result.json", "session.json"):
        p = os.path.join(d, name)
        if os.path.isfile(p):
            meta.update(json.load(open(p, encoding="utf-8")))
    out = parse_logs.analyse([d])
    perf = [p for p in out["perf"] if p["t"] >= 30]
    return meta, out, perf


def stats(vals):
    if not vals:
        return None
    return {"n": len(vals), "mean": statistics.mean(vals), "sd": statistics.pstdev(vals) if len(vals) > 1 else 0.0,
            "min": min(vals), "max": max(vals)}


def fmt(s, unit=""):
    return "-" if not s else f"{s['mean']:.2f}{unit} (sd {s['sd']:.2f}, {s['min']:.1f}..{s['max']:.1f}, n={s['n']})"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--min-gain", type=float, default=3.0, help="percent")
    a = ap.parse_args()
    (ma, oa, pa), (mb, ob, pb) = load(a.a), load(a.b)
    rows = []
    for label, key, unit in (("fps", "fps", ""), ("render thread", "render_ms", " ms"), ("GPU", "gpu_ms", " ms")):
        sa, sb = stats([p[key] for p in pa]), stats([p[key] for p in pb])
        rows.append((label, sa, sb, unit))
    print(f"A: {a.a}\n   build {ma.get('build_id', '?')}, scenario {ma.get('scenario', '?')}")
    print(f"B: {a.b}\n   build {mb.get('build_id', '?')}, scenario {mb.get('scenario', '?')}")
    for label, sa, sb, unit in rows:
        print(f"{label:14s} A {fmt(sa, unit):48s} B {fmt(sb, unit)}")
    for side, o in (("A", oa), ("B", ob)):
        prof = o.get("profile") or {}
        print(f"{side}: errors {sum(e['count'] for e in o['errors'])}, warnings {sum(w['count'] for w in o['warnings'])}, "
              f"crash hints {len(o['crash_hints'])}, run-time shader lines {o['runtime_shader_lines']}, "
              f"stack sampler {prof.get('sampling', 'n/a')}")
    verdict = "no measurable difference"
    sa, sb = rows[0][1], rows[0][2]
    if any((o.get("profile") or {}).get("sampling") == "active" for o in (oa, ob)):
        verdict = "not comparable: the stack sampler was on (its overhead distorts fps)"
    elif ma.get("scenario") and mb.get("scenario") and ma.get("scenario") != mb.get("scenario"):
        verdict = "not comparable: different scenarios"
    elif sa and sb:
        diff = sb["mean"] - sa["mean"]
        pct = 100.0 * diff / sa["mean"] if sa["mean"] else 0.0
        noise = max(sa["sd"], sb["sd"])
        if abs(pct) >= a.min_gain and abs(diff) > noise:
            verdict = f"B is {'faster' if diff > 0 else 'SLOWER'}: {diff:+.2f} fps ({pct:+.1f}%), above the noise ({noise:.2f})"
        else:
            verdict = f"no measurable difference ({diff:+.2f} fps, {pct:+.1f}%, noise {noise:.2f})"
    elif not sa or not sb:
        verdict = "not comparable: a session has no renut fps periods after 30 s"
    if ob["crash_hints"] and not oa["crash_hints"]:
        verdict += "; B CRASHED"
    print("verdict:", verdict)
    return 0


if __name__ == "__main__":
    sys.exit(main())
