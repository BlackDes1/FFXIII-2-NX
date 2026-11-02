#!/usr/bin/env python3
"""Prints XEX2 / XEXP optional headers: execution info, base, entry, static libraries (XDK version), delta-patch versions."""
import struct, sys
def ver(v): return f"{v>>28}.{(v>>24)&0xF}.{(v>>8)&0xFFFF}.{v&0xFF}"
def info(path):
    d = open(path, "rb").read()
    magic, flags, pe_off, _, sec_off, n = struct.unpack_from(">4sIIIII", d, 0)
    print(f"== {path}  magic={magic.decode()} flags=0x{flags:X} pe_off=0x{pe_off:X}")
    for i in range(n):
        k, v = struct.unpack_from(">II", d, 0x18 + i * 8)
        if k == 0x00010100: print(f"  entry point      0x{v:08X}")
        elif k == 0x00010201: print(f"  image base       0x{v:08X}")
        elif k == 0x00010001: print(f"  original base    0x{v:08X}")
        elif k == 0x00020200: print(f"  default stack    0x{v:X}")
        elif k == 0x00040006:
            mid, ve, bve, tid, plat, et, dn, dc, sg = struct.unpack_from(">IIIIBBBBI", d, v)
            print(f"  exec info        media=0x{mid:08X} version={ver(ve)} base_version={ver(bve)} title=0x{tid:08X} disc {dn}/{dc}")
        elif k == 0x000200FF:
            sz = struct.unpack_from(">I", d, v)[0]
            libs = []
            for o in range(v + 4, v + sz, 16):
                name = d[o:o+8].split(b"\0")[0].decode(); a, b, c, q = struct.unpack_from(">HHHH", d, o + 8)
                libs.append(f"{name} {a}.{b}.{c}.{q & 0x7FFF}")
            print("  static libs      " + ", ".join(libs))
        elif k == 0x000005FF:
            sz, tver, sver = struct.unpack_from(">III", d, v)
            print(f"  delta patch      source={ver(sver)} -> target={ver(tver)}")
        elif k == 0x000183FF:
            sz = struct.unpack_from(">I", d, v)[0]; print("  original PE name " + d[v+4:v+sz].split(b"\0")[0].decode())
        elif k == 0x000103FF:
            sz, strsz, cnt = struct.unpack_from(">III", d, v)
            names = d[v + 12: v + 12 + strsz].split(b"\0"); print("  imports          " + ", ".join(x.decode() for x in names if x))
        elif k == 0x000003FF:
            sz, enc, comp = struct.unpack_from(">IHH", d, v); print(f"  file format      encryption={enc} compression={comp}")
    if magic == b"XEX2":
        img_size = struct.unpack_from(">I", d, sec_off + 4)[0]; load = struct.unpack_from(">I", d, sec_off + 0x110)[0]
        print(f"  image size       0x{img_size:X}  load address 0x{load:08X}")
for p in sys.argv[1:]: info(p)
