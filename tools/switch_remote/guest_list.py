#!/usr/bin/env python3
"""ffxiii2-nx (2026-11-05, the missing audio): walks a guest linked list in the running game (nodes: +0 object, +4 next).

  python tools/switch_remote/guest_list.py --log <running game's renut log> --head 0x4000D910 [--max 200] [--words 4]

--head is the guest address of the word that holds the first node (the XAudio engine manager 0x4000D6C0's +592, the
voice list its pass processes). For each node: the object, its vtable and the object's next --words words. Read only.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from switch_remote import botbase, switchcfg  # noqa: E402
from switch_remote.xma_voices import guest_base  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    switchcfg.add_common_args(ap)
    ap.add_argument("--log", required=True)
    ap.add_argument("--head", type=lambda s: int(s, 0), required=True)
    ap.add_argument("--max", type=int, default=200)
    ap.add_argument("--words", type=int, default=4)
    a = ap.parse_args()
    cfg = switchcfg.from_args(a)
    base = guest_base(cfg, a.log)
    with botbase.connect(cfg) as b:
        u32 = lambda g: struct.unpack(">I", b.peek(base + g, 4, "absolute"))[0]  # noqa: E731
        node = u32(a.head)
        print(f"head 0x{a.head:08X} -> 0x{node:08X}")
        seen = set()
        for i in range(a.max):
            if node == 0 or node in seen or not 0x40000000 <= node < 0x50000000:
                print(f"end: 0x{node:08X}")
                break
            seen.add(node)
            obj, nxt = struct.unpack(">II", b.peek(base + node, 8, "absolute"))
            extra = ""
            if 0x40000000 <= obj < 0x50000000:
                words = struct.unpack(f">{a.words + 1}I", b.peek(base + obj, 4 * (a.words + 1), "absolute"))
                extra = " vtable %08X %s" % (words[0], " ".join(f"{w:08X}" for w in words[1:]))
            print(f"  [{i:3d}] node {node:08X}: obj {obj:08X}{extra}")
            node = nxt
    return 0


if __name__ == "__main__":
    sys.exit(main())
