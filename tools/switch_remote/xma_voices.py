#!/usr/bin/env python3
"""ffxiii2-nx (2026-11-05, the missing game audio): reads FF XIII-2's XMA voice table from the running game's memory.

  python tools/switch_remote/xma_voices.py --log <the running game's renut_NNN.log (read live, log_file_reopen)>

The voice loop sub_82B92708 walks 320 voice pointers at guest 0x82EB2058 and kicks a voice's contexts (register 0x650)
only when the voice's flag word (+4) has bit 0x4 set and 0x20000 clear; voice creation (sub_82B92B80) sets 0x20000 and
the submit function sub_82B92878 (reached through a vtable, slot 69) clears it after copying each 96-byte context to
the hardware pointer at context+64 and writing the kick. On the console no kick ever happens (2026-11-05 [audiodiag]).
This prints every non-null voice: flags, context count, and per context the first words and the hardware pointer, so
the PC (kicks) and the console (none) can be compared. Guest memory is big-endian; its host base is in the log
("Guest memory arena mapped: virtual base 0x...").
"""
import argparse
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from switch_remote import botbase, sdfiles, switchcfg  # noqa: E402

TABLE = 0x82EB2058
COUNT = 320


def guest_base(cfg, log_path):
    with sdfiles.open_sd(cfg, "botbase") as sd:
        text = sd.read(log_path, 0, 400000).decode("utf-8", "replace")
    m = re.search(r"Guest memory arena mapped: virtual base 0x([0-9A-Fa-f]+)", text)
    if not m:
        raise SystemExit(f"no guest memory base in {log_path} (is the log readable while the game runs?)")
    return int(m.group(1), 16)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    switchcfg.add_common_args(ap)
    ap.add_argument("--log", required=True, help="SD path of the running game's renut log")
    a = ap.parse_args()
    cfg = switchcfg.from_args(a)
    base = guest_base(cfg, a.log)
    print(f"guest base 0x{base:X}")
    with botbase.connect(cfg) as b:
        peek = lambda g, n: b.peek(base + g, n, "absolute")  # noqa: E731
        ptrs = struct.unpack(f">{COUNT}I", peek(TABLE, COUNT * 4))
        live = [(i, p) for i, p in enumerate(ptrs) if p]
        print(f"voice table 0x{TABLE:08X}: {len(live)} of {COUNT} entries set")
        for i, p in live[:24]:
            n, flags, ctxs = struct.unpack(">3I", peek(p, 12))
            state = ("start" if flags & 0x4 else "-") + (" not-ready(0x20000)" if flags & 0x20000 else " ready")
            print(f"  [{i:3d}] voice {p:08X}: contexts {n}, flags {flags:08X} ({state}), context array {ctxs:08X}")
            for c in range(min(n, 4)):
                w = struct.unpack(">24I", peek(ctxs + c * 96, 96))
                idx = struct.unpack(">H", peek(ctxs + c * 96 + 80, 2))[0]
                print(f"        ctx {c}: w0 {w[0]:08X} w1 {w[1]:08X} hw ptr {w[16]:08X} input {w[17]:08X} "
                      f"index {idx} (kick reg 0x{0x650 + (idx >> 5):03X} bit {idx & 31})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
