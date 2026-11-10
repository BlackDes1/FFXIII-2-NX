"""ffxiii2-nx: one interface for the console's SD card, over sys-botbase's file commands (primary) or FTP (fallback).

  with sdfiles.open_sd(cfg, "auto") as sd:
      sd.list("/switch/ffxiii2-nx/logs"); sd.size(p); sd.read(p); sd.write(p, data); sd.mkdirs(p); sd.rename(a, b)

"auto" tries sys-botbase first (one socket, binary-safe, no passive-mode stalls) and falls back to FTP if port 6000
does not answer. Every write goes to <path>.part first, is checked (size, and SHA-256 by reading it back unless
verify=False), and only then renamed over the target, so an interrupted transfer never leaves a half file under the
real name.
"""
import hashlib
import posixpath

from . import botbase, switchftp


class SdError(RuntimeError):
    pass


class _Base:
    kind = "?"

    def mkdirs(self, path):
        parts = [p for p in path.strip("/").split("/") if p]
        cur = ""
        for p in parts:
            cur += "/" + p
            if not self.exists(cur):
                self.mkdir(cur)

    def exists(self, path):
        parent, name = posixpath.split(path.rstrip("/"))
        try:
            return any(e["name"] == name for e in self.list(parent or "/"))
        except Exception:
            return False

    def sha256(self, path):
        return hashlib.sha256(self.read(path)).hexdigest()

    def put(self, path, data, verify=True, progress=None):
        tmp = path + ".part"
        if self.exists(tmp):
            self.delete(tmp)
        self.write_raw(tmp, data, progress)
        size = self.size(tmp)
        if size != len(data):
            raise SdError(f"{tmp}: {size} bytes on the SD card, {len(data)} sent")
        if verify:
            got = self.sha256(tmp)
            want = hashlib.sha256(data).hexdigest()
            if got != want:
                raise SdError(f"{tmp}: SHA-256 {got[:16]} on the SD card, {want[:16]} sent")
        if self.exists(path):
            self.delete(path)
        self.rename(tmp, path)


class BotBaseSd(_Base):
    kind = "botbase"

    def __init__(self, cfg):
        self.b = botbase.connect(cfg)

    def close(self):
        self.b.close()

    def list(self, path):
        return self.b.file_list(path)

    def size(self, path):
        return self.b.file_size(path)

    def read(self, path, offset=0, length=0):
        return self.b.file_read(path, offset, length)

    def write_raw(self, path, data, progress=None):
        self.b.file_write(path, data, progress)

    def delete(self, path):
        self.b.file_delete(path)

    def mkdir(self, path):
        self.b.file_mkdir(path)

    def rename(self, a, b):
        self.b.file_rename(a, b)


class FtpSd(_Base):
    kind = "ftp"

    def __init__(self, cfg):
        self.f = switchftp.connect(cfg)

    def close(self):
        self.f.close()

    def list(self, path):
        return self.f.list(path)

    def size(self, path):
        return self.f.size(path)

    def read(self, path, offset=0, length=0):
        data = self.f.read(path)
        return data[offset:offset + length] if length else data[offset:]

    def write_raw(self, path, data, progress=None):
        self.f.write(path, data)
        if progress:
            progress(len(data), len(data))

    def delete(self, path):
        self.f.delete(path)

    def mkdir(self, path):
        self.f.mkdir(path)

    def rename(self, a, b):
        self.f.rename(a, b)


class open_sd:
    def __init__(self, cfg, transport="auto"):
        self.cfg, self.transport, self.sd = cfg, transport, None

    def __enter__(self):
        errors = []
        order = {"auto": ("botbase", "ftp"), "botbase": ("botbase",), "ftp": ("ftp",)}[self.transport]
        for kind in order:
            try:
                self.sd = BotBaseSd(self.cfg) if kind == "botbase" else FtpSd(self.cfg)
                return self.sd
            except Exception as e:  # noqa: BLE001 - report every transport's reason
                errors.append(f"{kind}: {e}")
        raise SdError("no transport reached the console at " + str(self.cfg.get("switch_ip")) + ": " + "; ".join(errors))

    def __exit__(self, *exc):
        if self.sd:
            self.sd.close()
