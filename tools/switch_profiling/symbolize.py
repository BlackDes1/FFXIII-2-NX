#!/usr/bin/env python3
"""ffxiii2-nx: names for the addresses in the console's profiler and crash reports.

  python tools/switch_profiling/symbolize.py <rex_profile.log | rex_crash.log> [--elf <unstripped ELF>]
         [--build-id <id>] [--out named.txt] [--top 20]

The sampling profiler (sdk/src/ui/switch_perf.cpp; on with logs/rex/perfil_pilas.flag or stacks_profile.flag next to
the NRO, or switch_profile_stacks = true) and the crash handler write code addresses as image+0x<offset>. The offset
is from the start of the NRO's code, which is address 0 of the ELF the NRO was made from, so
`aarch64-none-elf-addr2line -f -C -e <ELF> 0x<offset>` names it. The ELF must be that exact build's: deploy.py keeps
it in <local_builds_dir>/<slot>/<build_id>/, and --build-id (or the build_id of the log session's session.json) finds
it there.

Output: the log with every image+0x.. followed by [function file:line], then a summary per thread: the functions with
the most samples ("pc" lines), their callers ("lr" / "stack" lines), and the frames seen during hitches
(frames over 45 ms). The percentages are shares of samples; the sampler itself slows the game, so its fps are not
benchmarks.
"""
import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, TOOLS)
from switch_remote import switchcfg  # noqa: E402

RE_ADDR = re.compile(r"image\+0x([0-9a-fA-F]+)")
RE_THREAD = re.compile(r'^-- thread (?:(\d+) )?"([^"]*)"')
RE_PCT = re.compile(r"^\s+([0-9.]+)%\s+(pc|lr|lrleaf|stack|wait)\b(.*)$")


def find_addr2line(explicit=None):
    for c in (explicit, os.environ.get("ADDR2LINE"), "C:/devkitPro/devkitA64/bin/aarch64-none-elf-addr2line.exe",
              "/opt/devkitpro/devkitA64/bin/aarch64-none-elf-addr2line", shutil.which("aarch64-none-elf-addr2line"),
              shutil.which("llvm-addr2line"), shutil.which("addr2line")):
        if c and os.path.isfile(c):
            return c
    raise SystemExit("aarch64-none-elf-addr2line not found (devkitA64); pass --addr2line or set ADDR2LINE")


def find_elf(a, cfg, log):
    if a.elf:
        return a.elf
    bid = a.build_id
    if not bid:
        d = os.path.dirname(os.path.abspath(log))
        for _ in range(4):
            sj = os.path.join(d, "session.json")
            if os.path.isfile(sj):
                bid = json.load(open(sj, encoding="utf-8")).get("build_id")
                break
            d = os.path.dirname(d)
    if bid and bid != "unknown":
        for d in glob.glob(os.path.join(cfg["local_builds_dir"], "*", bid)):
            mf = os.path.join(d, "manifest.json")
            if os.path.isfile(mf):  # deploy.py keeps the ELF under its own name (the Switch build's is "renut")
                name = json.load(open(mf, encoding="utf-8")).get("elf", {}).get("name")
                if name and os.path.isfile(os.path.join(d, name)):
                    return os.path.join(d, name)
            hits = glob.glob(os.path.join(d, "*.elf"))
            if hits:
                return hits[0]
    raise SystemExit(f"no ELF for build {bid!r}: pass --elf (the unstripped ELF of the NRO that wrote the log)")


def resolve(addr2line, elf, offsets, base=0):
    names = {}
    offsets = sorted(set(offsets))
    for i in range(0, len(offsets), 4000):
        part = offsets[i:i + 4000]
        r = subprocess.run([addr2line, "-f", "-C", "-e", elf], input="\n".join(f"0x{o + base:x}" for o in part) + "\n",
                           capture_output=True, text=True)
        lines = r.stdout.splitlines()
        for k, o in enumerate(part):
            fn = lines[2 * k] if 2 * k < len(lines) else "??"
            loc = lines[2 * k + 1] if 2 * k + 1 < len(lines) else "??:0"
            loc = loc.split(" (discriminator")[0]
            if "/" in loc or "\\" in loc:
                path, _, line = loc.rpartition(":")
                loc = f"{os.path.basename(path)}:{line}"
            names[o] = (fn, loc)
    return names


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--elf")
    ap.add_argument("--build-id")
    ap.add_argument("--addr2line")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=0, help="ELF address of image+0 (default 0)")
    ap.add_argument("--out")
    ap.add_argument("--top", type=int, default=20)
    ap.add_argument("--config")
    a = ap.parse_args()
    cfg = switchcfg.load(a.config, need_ip=False)
    text = open(a.log, encoding="utf-8", errors="replace").read()
    offs = [int(m.group(1), 16) for m in RE_ADDR.finditer(text)]
    if not offs:
        print("no image+0x addresses in the log (stack sampling disabled? see perfil_pilas.flag)")
        return 1
    elf = find_elf(a, cfg, a.log)
    names = resolve(find_addr2line(a.addr2line), elf, offs, a.base)

    def name(m):
        fn, loc = names.get(int(m.group(1), 16), ("??", "??:0"))
        return f"{m.group(0)} [{fn} {loc}]"
    named = RE_ADDR.sub(name, text)
    out = a.out or os.path.splitext(a.log)[0] + "_named.txt"
    with open(out, "w", encoding="utf-8") as f:
        f.write(named)

    # summary: per thread, functions by pc share, callers by lr/stack share; hitch block kept apart
    summary = []
    thread, section, report = None, "normal", 0
    per, seen = {}, {}
    for line in text.splitlines():
        if line.startswith("== during the hitches"):
            section = "hitch"
        elif line.startswith("==== "):
            section, report = "normal", report + 1
        t = RE_THREAD.match(line)
        if t:
            thread = (section, f"{t.group(2)} [{t.group(1)}]" if t.group(1) else t.group(2))
            seen.setdefault(thread, set()).add(report)
            continue
        p = RE_PCT.match(line)
        if p and thread:
            pct, kind, rest = float(p.group(1)), p.group(2), p.group(3)
            addrs = [int(x, 16) for x in RE_ADDR.findall(rest)]
            if not addrs:
                continue
            fn, loc = names.get(addrs[0], ("??", "??:0"))
            caller = names.get(addrs[1], ("", ""))[0] if len(addrs) > 1 else ""
            d = per.setdefault(thread, {}).setdefault(kind, {})
            k = (fn, loc, caller if kind == "stack" else "")
            d[k] = d.get(k, 0.0) + pct
    # each report gives shares of that report's samples: average over the reports the thread was in
    for th, kinds in per.items():
        n = max(1, len(seen.get(th, ())))
        for kind in kinds.values():
            for k in kind:
                kind[k] /= n
    for (section, th), kinds in per.items():
        summary.append(f"\n## {'HITCHES (frames over 45 ms) ' if section == 'hitch' else ''}thread \"{th}\" "
                       f"(average over {len(seen.get((section, th), ()))} reports)")
        for kind in ("pc", "lr", "stack", "wait"):
            if kind not in kinds:
                continue
            summary.append(f"   {kind}:" + {"pc": " where the CPU was", "lr": " called from",
                                             "stack": " function <- caller", "wait": " waiting in the kernel"}[kind])
            for (fn, loc, caller), pct in sorted(kinds[kind].items(), key=lambda x: -x[1])[:a.top]:
                summary.append(f"   {pct:6.1f}%  {fn}  ({loc})" + (f"  <- {caller}" if caller else ""))
    with open(out, "a", encoding="utf-8") as f:
        f.write("\n\n#### SUMMARY (sample shares; profiler overhead makes its fps no benchmark)\n")
        f.write("\n".join(summary) + "\n")
    print("\n".join(summary[: 12 * max(1, a.top)]))
    print(f"\n{len(set(offs))} addresses named with {os.path.basename(elf)} -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
