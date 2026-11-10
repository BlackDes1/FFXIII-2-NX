#!/usr/bin/env python3
"""ffxiii2-nx: a tiny FTP server (passive mode, binary) over a local folder, to test switchftp.py without the
console. With flaky=N every Nth data transfer is dropped (the control connection closed), like the stalls the
sys-ftpd-light README describes, so the client's retry path runs.
"""
import os
import socket
import socketserver
import threading


class Handler(socketserver.StreamRequestHandler):
    def reply(self, s):
        self.wfile.write((s + "\r\n").encode())
        self.wfile.flush()

    def path(self, p):
        p = p if p.startswith("/") else self.cwd.rstrip("/") + "/" + p
        return os.path.join(self.server.root, p.lstrip("/").replace("/", os.sep))

    def data_conn(self):
        self.server.transfers += 1
        if self.server.flaky and self.server.transfers % self.server.flaky == 0:
            self.pasv.close()
            raise ConnectionAbortedError("simulated stall")
        c, _ = self.pasv.accept()
        self.pasv.close()
        return c

    def handle(self):
        self.cwd, self.user, self.ok, self.pasv, self.rnfr = "/", None, False, None, None
        self.reply("220 mock sys-ftpd")
        try:
            while True:
                line = self.rfile.readline().decode("latin-1").rstrip("\r\n")
                if not line:
                    return
                cmd, _, arg = line.partition(" ")
                cmd = cmd.upper()
                if cmd == "USER":
                    self.user = arg
                    self.reply("331 password")
                elif cmd == "PASS":
                    self.ok = (self.user, arg) == self.server.cred
                    self.reply("230 ok" if self.ok else "530 Unknown user or password")
                elif not self.ok:
                    self.reply("530 login first")
                elif cmd == "TYPE":
                    self.reply("200 binary")
                elif cmd == "PASV":
                    self.pasv = socket.socket()
                    self.pasv.bind(("127.0.0.1", 0))
                    self.pasv.listen(1)
                    port = self.pasv.getsockname()[1]
                    self.reply(f"227 Entering Passive Mode (127,0,0,1,{port >> 8},{port & 255})")
                elif cmd == "LIST":
                    d = self.path(arg or self.cwd)
                    self.reply("150 listing")
                    c = self.data_conn()
                    for n in sorted(os.listdir(d)):
                        p = os.path.join(d, n)
                        kind = "d" if os.path.isdir(p) else "-"
                        size = 0 if os.path.isdir(p) else os.path.getsize(p)
                        c.sendall(f"{kind}rwxrwxrwx 1 root root {size} Nov 05 12:00 {n}\r\n".encode())
                    c.close()
                    self.reply("226 done")
                elif cmd == "SIZE":
                    p = self.path(arg)
                    self.reply(f"213 {os.path.getsize(p)}" if os.path.isfile(p) else "550 no file")
                elif cmd == "RETR":
                    p = self.path(arg)
                    if not os.path.isfile(p):
                        self.reply("550 no file")
                        continue
                    self.reply("150 sending")
                    c = self.data_conn()
                    c.sendall(open(p, "rb").read())
                    c.close()
                    self.reply("226 done")
                elif cmd == "STOR":
                    self.reply("150 receiving")
                    c = self.data_conn()
                    with open(self.path(arg), "wb") as f:
                        while True:
                            b = c.recv(1 << 16)
                            if not b:
                                break
                            f.write(b)
                    c.close()
                    self.reply("226 done")
                elif cmd == "DELE":
                    try:
                        os.remove(self.path(arg))
                        self.reply("250 deleted")
                    except OSError:
                        self.reply("550 failed")
                elif cmd == "MKD":
                    try:
                        os.mkdir(self.path(arg))
                        self.reply(f'257 "{arg}"')
                    except OSError:
                        self.reply("550 failed")
                elif cmd == "RNFR":
                    self.rnfr = self.path(arg)
                    self.reply("350 ready")
                elif cmd == "RNTO":
                    try:
                        os.rename(self.rnfr, self.path(arg))
                        self.reply("250 renamed")
                    except OSError:
                        self.reply("550 failed")
                elif cmd == "QUIT":
                    self.reply("221 bye")
                    return
                else:
                    self.reply("502 not implemented")
        except (ConnectionAbortedError, ConnectionResetError, BrokenPipeError):
            return


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def start(root, user, password, flaky=0):
    srv = Server(("127.0.0.1", 0), Handler)
    srv.root, srv.cred, srv.flaky, srv.transfers = root, (user, password), flaky, 0
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv
