"""ffxiii2-nx: FTP client for sys-ftpd-light (Thorhax fork 1.0.5-thorhax1, port 5000, config
sdmc:/config/sys-ftpd-10k/config.ini).

The fork's README names a known limit: poll() on the console can miss a passive-mode data connection that arrives
early, so about half of the listings and transfers of standard clients stall for a few seconds. Every operation here
therefore runs with a short socket timeout and is retried on a fresh control connection (up to cfg retries), and
every transfer is checked: uploads with SIZE (and a read-back hash with verify=True), downloads against SIZE.
sys-botbase's file commands (botbase.py) are the primary transport; this is the fallback.
"""
import ftplib
import hashlib
import io
import posixpath
import socket
import time


class FtpError(RuntimeError):
    pass


class SwitchFTP:
    def __init__(self, host, port=5000, user="", password="", timeout=15.0, retries=3):
        if not user or not password:
            raise FtpError("ftp_user / ftp_password are empty in switch_test_config.json (anonymous access is off)")
        self.host, self.port, self.user, self.password = host, port, user, password
        self.timeout, self.retries = timeout, retries
        self.ftp = None

    def _connect(self):
        self.close()
        f = ftplib.FTP(timeout=self.timeout)
        f.connect(self.host, self.port)
        f.login(self.user, self.password)
        f.set_pasv(True)
        try:
            f.voidcmd("TYPE I")
        except ftplib.error_perm:
            pass
        self.ftp = f

    def close(self):
        if self.ftp:
            try:
                self.ftp.quit()
            except Exception:
                try:
                    self.ftp.close()
                except Exception:
                    pass
            self.ftp = None

    def __enter__(self):
        self._connect()
        return self

    def __exit__(self, *exc):
        self.close()

    def _retry(self, what, fn):
        last = None
        for attempt in range(1, self.retries + 1):
            try:
                if self.ftp is None:
                    self._connect()
                return fn()
            except ftplib.error_perm as e:  # a definite answer (no such file, ...): no retry
                raise FtpError(f"{what}: {e}")
            except (OSError, EOFError, ftplib.Error, socket.timeout) as e:
                last = e
                self.close()
                time.sleep(min(2.0 * attempt, 6.0))
        raise FtpError(f"{what}: failed after {self.retries} attempts ({last})")

    # --- operations -------------------------------------------------------------------------------------------
    def list(self, path):
        def run():
            lines = []
            self.ftp.retrlines(f"LIST {path}", lines.append)
            out = []
            for line in lines:
                parts = line.split(None, 8)
                if len(parts) < 9:
                    continue
                kind = "dir" if line.startswith("d") else "file"
                out.append({"type": kind, "name": parts[8], "size": None if kind == "dir" else int(parts[4]),
                            "mtime": " ".join(parts[5:8])})
            return out
        return self._retry(f"LIST {path}", run)

    def size(self, path):
        def run():
            r = self.ftp.sendcmd(f"SIZE {path}")
            return int(r.split()[-1])
        return self._retry(f"SIZE {path}", run)

    def read(self, path):
        expected = self.size(path)

        def run():
            buf = io.BytesIO()
            self.ftp.retrbinary(f"RETR {path}", buf.write, blocksize=1 << 16)
            data = buf.getvalue()
            if len(data) != expected:
                raise OSError(f"short download {len(data)} of {expected}")
            return data
        return self._retry(f"RETR {path}", run)

    def write(self, path, data, verify=False):
        def run():
            self.ftp.storbinary(f"STOR {path}", io.BytesIO(data), blocksize=1 << 16)
            got = int(self.ftp.sendcmd(f"SIZE {path}").split()[-1])
            if got != len(data):
                raise OSError(f"size after upload {got} != {len(data)}")
        self._retry(f"STOR {path}", run)
        if verify and hashlib.sha256(self.read(path)).digest() != hashlib.sha256(data).digest():
            raise FtpError(f"{path}: read-back hash differs after upload")

    def delete(self, path):
        self._retry(f"DELE {path}", lambda: self.ftp.delete(path))

    def mkdir(self, path):
        self._retry(f"MKD {path}", lambda: self.ftp.mkd(path))

    def rename(self, src, dst):
        self._retry(f"RNFR {src}", lambda: self.ftp.rename(src, dst))

    def exists(self, path):
        parent, name = posixpath.split(path.rstrip("/"))
        try:
            return any(e["name"] == name for e in self.list(parent or "/"))
        except FtpError:
            return False


def connect(cfg):
    f = SwitchFTP(cfg["switch_ip"], int(cfg.get("ftp_port", 5000)), cfg.get("ftp_user", ""), cfg.get("ftp_password", ""),
                  timeout=float(cfg.get("timeout_s", 10.0)) + 5.0, retries=int(cfg.get("retries", 3)))
    f._connect()
    return f
