#!/usr/bin/env python3
"""ffxiii2-nx (2026-11-05, the missing game audio): finds the audio stream objects in the running game's memory.

  python tools/switch_remote/xma_objects.py --log <the running game's renut_NNN.log> [--start 0x40000000] [--size 0x400000]

The PC (run aud5, ffxiii2_audio_diag) submits the XMA voice every 5-10 ms from sub_82BD4DA0 (a virtual function of the
object with vtable 0x821F644C, at 0x4009C0E0 on the PC) through the object at its +268 (vtable 0x821D7ED8, slot 7 =
sub_82B8E950, the submit; its +336 is the voice, 0xE4687000 on both machines). On the PC the stream object's words are
+8 0, +80 this+0x44, +112 0, +116 1, +120 0, +256 / +260 two objects, +268 the XMA object. This scans guest memory for
both vtables and prints the same words of every object found, to compare with the PC. Read only.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from switch_remote import botbase, switchcfg  # noqa: E402
from switch_remote.xma_voices import guest_base  # noqa: E402

STREAM_VTABLE = 0x821F644C
XMA_VTABLE = 0x821D7ED8
CHUNK = 0x10000


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    switchcfg.add_common_args(ap)
    ap.add_argument("--log", required=True)
    ap.add_argument("--start", type=lambda s: int(s, 0), default=0x40000000)
    ap.add_argument("--size", type=lambda s: int(s, 0), default=0x400000)
    a = ap.parse_args()
    cfg = switchcfg.from_args(a)
    base = guest_base(cfg, a.log)
    print(f"guest base 0x{base:X}; scanning 0x{a.start:08X}-0x{a.start + a.size:08X}")
    found = []
    chunks = []
    with botbase.connect(cfg) as b:
        peek = lambda g, n: b.peek(base + g, n, "absolute")  # noqa: E731
        for off in range(0, a.size, CHUNK):
            data = peek(a.start + off, CHUNK)
            chunks.append((a.start + off, data))
            for i in range(0, len(data) - 3, 4):
                w = struct.unpack_from(">I", data, i)[0]
                if w in (STREAM_VTABLE, XMA_VTABLE):
                    found.append((a.start + off + i, w))
        # 2026-11-05 (PC aud8): the voice callback is reached from sub_82BB9C80, which walks a list at its manager's
        # +616 (nodes: +0 object, +4 next) and calls each object's slot 0. Who points at the stream objects here?
        streams = {addr for addr, vt in found if vt == STREAM_VTABLE}
        for start, data in chunks:
            for i in range(0, len(data) - 3, 4):
                w = struct.unpack_from(">I", data, i)[0]
                if w in streams:
                    lo = max(0, i - 8)
                    ctx_words = " ".join(f"{x:08X}" for x in struct.unpack_from(f">{(min(len(data), i + 12) - lo) // 4}I", data, lo))
                    print(f"  ref to {w:08X} at {start + i:08X}: {ctx_words}")
        print(f"{len(found)} objects")
        for addr, vt in found[:40]:
            words = struct.unpack(">96I", peek(addr, 384))
            if vt == STREAM_VTABLE:
                print(f"  stream {addr:08X}: +8 {words[2]:08X} +80 {words[20]:08X} +112 {words[28]:08X} "
                      f"+116 {words[29]:08X} +120 {words[30]:08X} +256 {words[64]:08X} +260 {words[65]:08X} "
                      f"+264 {words[66]:08X} +268 {words[67]:08X}")
            else:
                print(f"  xma    {addr:08X}: +336 voice {words[84]:08X}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
