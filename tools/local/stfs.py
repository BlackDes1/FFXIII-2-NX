#!/usr/bin/env python3
"""Minimal read-only STFS (LIVE/PIRS/CON) package extractor; every block is checked against its SHA-1 hash entry.
usage: stfs.py PACKAGE [-o OUTDIR] [--list]"""
import argparse, hashlib, os, struct, sys

class Stfs:
    def __init__(s, path):
        s.f = open(path, "rb")
        h = s.f.read(0x1000)
        s.magic = h[:4]
        s.header_size = struct.unpack_from(">I", h, 0x340)[0]
        vd = h[0x379:0x379 + 0x24]
        s.sep = vd[2]
        s.ft_count = struct.unpack_from("<H", vd, 3)[0]
        s.ft_start = int.from_bytes(vd[5:8], "little")
        s.alloc = struct.unpack_from(">I", vd, 0x1C)[0]
        s.sex = (~s.sep) & 1               # 0 = female (one hash table per level), 1 = male (two copies)
        s.step0 = 0xAB if s.sex == 0 else 0xAC
        s.base = (s.header_size + 0xFFF) & ~0xFFF
        s.title_id = struct.unpack_from(">I", h, 0x360)[0]
        s.content_type = struct.unpack_from(">I", h, 0x344)[0]
        s.display = h[0x411:0x411 + 0x80].decode("utf-16-be", "ignore").split("\0")[0]
        s.bad = 0

    def data_block(s, b):
        sx = s.sex
        r = (((b + 0xAA) // 0xAA) << sx) + b
        if b < 0xAA: return r
        if b < 0x70E4: return r + (((b + 0x70E4) // 0x70E4) << sx)
        return (1 << sx) + r + (((b + 0x70E4) // 0x70E4) << sx)

    def l0_hash_block(s, b):
        if b < 0xAA: return 0
        n = (b // 0xAA) * s.step0
        n += ((b // 0x70E4) + 1) << s.sex
        if b // 0x70E4 == 0: return n
        return n + (1 << s.sex)

    def read_block(s, b):
        s.f.seek(s.base + (s.data_block(b) << 12)); return s.f.read(0x1000)

    def hash_entry(s, b):
        # try each table copy; return the entry whose hash matches the data
        data = s.read_block(b)
        sha = hashlib.sha1(data).digest()
        hb = s.l0_hash_block(b)
        for copy in range(1 + s.sex):
            s.f.seek(s.base + ((hb + copy) << 12) + (b % 0xAA) * 0x18)
            e = s.f.read(0x18)
            if e[:20] == sha:
                return data, int.from_bytes(e[0x15:0x18], "big"), True
        s.f.seek(s.base + (hb << 12) + (b % 0xAA) * 0x18); e = s.f.read(0x18)
        return data, int.from_bytes(e[0x15:0x18], "big"), False

    def chain(s, start, count, consecutive):
        b = start
        for i in range(count):
            data, nxt, ok = s.hash_entry(b)
            if not ok: s.bad += 1
            yield data
            b = b + 1 if consecutive else nxt

    def entries(s):
        raw = b"".join(s.chain(s.ft_start, s.ft_count, False))
        out = []
        for i in range(0, len(raw), 0x40):
            e = raw[i:i + 0x40]
            nl = e[0x28] & 0x3F
            if nl == 0: continue
            out.append(dict(idx=len(out), name=e[:nl].decode("latin-1"), dir=bool(e[0x28] & 0x80),
                            cons=bool(e[0x28] & 0x40), blocks=int.from_bytes(e[0x29:0x2C], "little"),
                            start=int.from_bytes(e[0x2F:0x32], "little"),
                            parent=struct.unpack_from(">h", e, 0x32)[0], size=struct.unpack_from(">I", e, 0x34)[0]))
        return out

    def path(s, ents, e):
        p = [e["name"]]
        while e["parent"] != -1:
            e = ents[e["parent"]]; p.append(e["name"])
        return "/".join(reversed(p))

ap = argparse.ArgumentParser(); ap.add_argument("pkg"); ap.add_argument("-o"); ap.add_argument("--list", action="store_true")
a = ap.parse_args()
s = Stfs(a.pkg)
print(f"magic={s.magic!r} title={s.title_id:08X} type={s.content_type:08X} name={s.display!r} sex={'male' if s.sex else 'female'} ft@{s.ft_start}x{s.ft_count}", file=sys.stderr)
ents = s.entries()
for e in ents:
    p = s.path(ents, e)
    if a.list or e["dir"]:
        print(f"{'<dir>' if e['dir'] else e['size']:>12}  {p}"); 
        if e["dir"]: continue
    if a.o and not e["dir"]:
        dst = os.path.join(a.o, *p.split("/")); os.makedirs(os.path.dirname(dst), exist_ok=True)
        left = e["size"]
        with open(dst, "wb") as w:
            for data in s.chain(e["start"], e["blocks"], e["cons"]):
                w.write(data[:left]); left -= min(left, 0x1000)
        print("extracted", p, e["size"], file=sys.stderr)
print(f"hash mismatches: {s.bad}", file=sys.stderr)
