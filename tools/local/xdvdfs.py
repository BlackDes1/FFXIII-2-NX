#!/usr/bin/env python3
"""Minimal read-only XDVDFS (Xbox 360 disc) lister/extractor.
usage: xdvdfs.py ISO --list | ISO -o OUTDIR [--only GLOB ...]"""
import argparse, fnmatch, os, struct, sys, time
SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
BASES = [0x0, 0x0FD90000, 0x02080000, 0x18300000]

def find_base(f):
    for b in BASES:
        f.seek(b + 32 * SECTOR)
        if f.read(20) == MAGIC:
            return b
    raise SystemExit("no XDVDFS volume descriptor found")

def walk(f, base, sector, size, prefix, out):
    f.seek(base + sector * SECTOR)
    data = f.read(size)
    stack = [0]
    seen = set()
    while stack:
        off = stack.pop()
        if off in seen or off + 14 > len(data):
            continue
        seen.add(off)
        l, r, start, fsize, attr, nlen = struct.unpack_from("<HHIIBB", data, off)
        if l == 0xFFFF and r == 0xFFFF:
            continue
        name = data[off + 14: off + 14 + nlen].decode("latin-1")
        path = prefix + name
        if attr & 0x10:
            if fsize:
                walk(f, base, start, fsize, path + "/", out)
        else:
            out.append((path, start, fsize))
        if l: stack.append(l * 4)
        if r: stack.append(r * 4)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("iso"); ap.add_argument("-o"); ap.add_argument("--list", action="store_true")
    ap.add_argument("--only", nargs="*"); ap.add_argument("--seconds", type=float, default=0)
    a = ap.parse_args()
    a.deadline = time.time() + a.seconds if a.seconds else 0
    with open(a.iso, "rb") as f:
        base = find_base(f)
        f.seek(base + 32 * SECTOR + 20)
        root, rsize = struct.unpack("<II", f.read(8))
        files = []
        walk(f, base, root, rsize, "", files)
        files.sort()
        print(f"base=0x{base:X} files={len(files)} total={sum(s for _,_,s in files)/2**30:.2f} GiB", file=sys.stderr)
        for p, s, n in files:
            if a.only and not any(fnmatch.fnmatch(p.lower(), g.lower()) for g in a.only):
                continue
            if a.list:
                print(f"{n:12d}  {p}")
                continue
            dst = os.path.join(a.o, *p.split("/"))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            if os.path.exists(dst) and os.path.getsize(dst) == n:
                continue
            done = os.path.getsize(dst + ".part") if os.path.exists(dst + ".part") else 0   # resume a partial file
            f.seek(base + s * SECTOR + done); left = n - done
            with open(dst + ".part", "ab") as w:
                while left:
                    if a.deadline and time.time() > a.deadline: print("deadline: stopped in", p, file=sys.stderr); return
                    c = f.read(min(left, 8 << 20)); w.write(c); left -= len(c)
            os.replace(dst + ".part", dst)
            print("extracted", p, n, file=sys.stderr)
main()
