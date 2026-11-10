#!/usr/bin/env python3
"""ffxiii2-nx: a stand-in for sys-botbase 2.6-files-kill (same wire protocol, an SD card in a local folder), so the
tools can be tested without the console:

  python tools/switch_tests/mock_botbase.py --root <folder used as SD card> [--port 6000]
  python tools/switch_tests/selftest.py            (starts one itself and runs every tool against it)

It answers like the fork's main.c / files.c: text lines, file replies on the socket, no reply for input commands.
It records the input commands in .inputs.log under the root, so tests can check that every button was released.
"""
import argparse
import os
import socket
import socketserver
import threading

JPG = bytes.fromhex("ffd8ffe000104a46494600010100000100010000ffdb004300" + "08" * 64 + "ffd9")
NO_REPLY = {"press", "release", "click", "setStick", "clickSeq", "clickCancel", "configure", "detachController",
            "touch", "touchHold", "touchDraw", "touchCancel", "key", "keyMod", "keyMulti"}


class State:
    title = "0100000000000000"  # a "running application" until killGame


class Handler(socketserver.StreamRequestHandler):
    def sd(self, p):
        p = p[5:] if p.startswith("sdmc:") else p
        return os.path.join(self.server.root, p.lstrip("/").replace("/", os.sep))

    def out(self, s):
        self.wfile.write(s.encode("latin-1") if isinstance(s, str) else s)
        self.wfile.flush()

    def handle(self):
        while True:
            line = self.rfile.readline()
            if not line:
                return
            argv = line.decode("latin-1").strip().split()
            if not argv:
                continue
            c = argv[0]
            if getattr(self.server, "pending_done", False) and c not in NO_REPLY:
                self.server.pending_done = False
                self.out("done\n")
            if c in NO_REPLY:
                with open(os.path.join(self.server.root, ".inputs.log"), "a") as f:
                    f.write(" ".join(argv) + "\n")
                if c == "clickSeq":
                    self.server.pending_done = True  # flushed with the next command's reply, as sys-botbase does
                continue
            if c == "getVersion":
                self.out("2.6-files-kill\n")
            elif c == "getTitleID":
                self.out(State.title + "\n" if State.title else "\n")
            elif c in ("getTitleVersion", "getHeapBase", "getMainNsoBase"):
                self.out("0000000000000000\n")
            elif c == "getBuildID":
                self.out("0011223344556677\n")
            elif c == "game":
                self.out("mock\n")
            elif c == "charge":
                self.out("87\n")
            elif c == "pixelPeek":
                self.out(JPG.hex().upper() + "\n")
            elif c == "killGame":
                self.out("ok\n" if State.title else "err none\n")
                State.title = ""
            elif c in ("peek", "peekMain", "peekAbsolute"):
                self.out("00" * int(argv[2]) + "\n")
            elif c == "fileList":
                d = self.sd(argv[1])
                if os.path.isdir(d):
                    for n in sorted(os.listdir(d)):
                        if n.startswith(".inputs"):
                            continue
                        p = os.path.join(d, n)
                        self.out(f"D {n}\n" if os.path.isdir(p) else f"F {os.path.getsize(p)} {n}\n")
                self.out("\n")
            elif c == "fileSize":
                p = self.sd(argv[1])
                self.out(f"{os.path.getsize(p) if os.path.isfile(p) else -1}\n")
            elif c == "fileRead":
                p = self.sd(argv[1])
                off = int(argv[2], 0) if len(argv) > 2 else 0
                n = int(argv[3], 0) if len(argv) > 3 else 0
                if not os.path.isfile(p):
                    self.out("-1\n")
                    continue
                with open(p, "rb") as f:
                    f.seek(off)
                    data = f.read(n) if n else f.read()
                self.out(f"{len(data)}\n".encode() + data)
            elif c in ("fileWrite", "fileAppend"):
                size = int(argv[2])
                data = self.rfile.read(size)
                p = self.sd(argv[1])
                try:
                    with open(p, "wb" if c == "fileWrite" else "ab") as f:
                        f.write(data)
                    self.out(f"ok {len(data)}\n")
                except OSError:
                    self.out(f"err {len(data)}\n")
            elif c == "fileDelete":
                p = self.sd(argv[1])
                try:
                    os.rmdir(p) if os.path.isdir(p) else os.remove(p)
                    self.out("ok\n")
                except OSError:
                    self.out("err\n")
            elif c == "fileMkdir":
                try:
                    os.mkdir(self.sd(argv[1]))
                    self.out("ok\n")
                except OSError:
                    self.out("err\n")
            elif c == "fileRename":
                try:
                    os.rename(self.sd(argv[1]), self.sd(argv[2]))
                    self.out("ok\n")
                except OSError:
                    self.out("err\n")
            else:
                self.out("\n")


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def start(root, port=0):
    srv = Server(("127.0.0.1", port), Handler)
    srv.root = root
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--port", type=int, default=6000)
    a = ap.parse_args()
    srv = start(a.root, a.port)
    print("mock sys-botbase on", srv.server_address)
    threading.Event().wait()


if __name__ == "__main__":
    main()
