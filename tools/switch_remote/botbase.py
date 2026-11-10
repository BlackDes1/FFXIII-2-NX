"""ffxiii2-nx: client for sys-botbase, masagrator fork 2.6-kill (version string "2.6-files-kill").

Protocol (the fork's commands.md and source/main.c, files.c): one text command per line over TCP (port 6000), the
reply written straight to the socket. Text replies end in "\\n". Commands that only act (press, release, click,
setStick, clickSeq, configure, touch...) send no reply. The fork's file commands answer on the same socket:
  fileList <dir>               "D <name>" / "F <size> <name>" lines, then an empty line
  fileSize <path>              "<size>" or "-1"
  fileRead <path> [off] [len]  "<n>\\n" then exactly n raw bytes; "-1\\n" on error
  fileWrite|fileAppend <path> <size>, then exactly size raw bytes -> "ok <n>" / "err <n>"
  fileDelete|fileMkdir <path>, fileRename <a> <b> -> "ok" / "err"
  killGame                     "ok", "err none" (nothing running) or "err <hex result>"
Paths are absolute SD paths without spaces. pixelPeek answers with the JPG as one line of hex.

Only the Python standard library is used (the PC jobs run C:\\Python314\\python.exe).
"""
import socket
import time

BUTTONS = ("A", "B", "X", "Y", "RSTICK", "LSTICK", "L", "R", "ZL", "ZR", "PLUS", "MINUS",
           "DLEFT", "DUP", "DRIGHT", "DDOWN", "HOME", "CAPTURE")
ALIASES = {"L3": "LSTICK", "R3": "RSTICK", "DL": "DLEFT", "DU": "DUP", "DR": "DRIGHT", "DD": "DDOWN",
           "LEFT": "DLEFT", "UP": "DUP", "RIGHT": "DRIGHT", "DOWN": "DDOWN", "START": "PLUS", "SELECT": "MINUS"}
EXPECTED_VERSION = "2.6-files-kill"
CHUNK = 1 << 20  # bytes per fileWrite / fileAppend call


class BotBaseError(RuntimeError):
    pass


def button(name):
    n = name.strip().upper()
    n = ALIASES.get(n, n)
    if n not in BUTTONS:
        raise ValueError(f"unknown button {name!r} (known: {', '.join(BUTTONS)} and {', '.join(ALIASES)})")
    return n


def check_path(path):
    if not path.startswith("/") and not path.startswith("sdmc:/"):
        raise ValueError(f"SD path must be absolute: {path!r}")
    if any(c.isspace() for c in path):
        raise ValueError(f"sys-botbase file commands cannot take spaces: {path!r}")
    return path


class BotBase:
    def __init__(self, host, port=6000, timeout=10.0):
        self.host, self.port, self.timeout = host, port, timeout
        self.sock = None
        self._buf = b""
        self.pressed = set()

    # --- connection -------------------------------------------------------------------------------------------
    def connect(self):
        self.sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._buf = b""
        return self

    def close(self):
        if self.sock:
            try:
                self.release_all()
            except Exception:
                pass
            try:
                self.sock.close()
            finally:
                self.sock = None

    def __enter__(self):
        return self.connect() if self.sock is None else self

    def __exit__(self, *exc):
        self.close()

    # --- low level --------------------------------------------------------------------------------------------
    def send(self, line, payload=b""):
        if self.sock is None:
            raise BotBaseError("not connected")
        self.sock.sendall(line.encode("ascii") + b"\r\n" + payload)

    def _fill(self):
        chunk = self.sock.recv(1 << 16)
        if not chunk:
            raise BotBaseError("connection closed by the console")
        self._buf += chunk

    def readline(self, timeout=None):
        if timeout is not None:
            self.sock.settimeout(timeout)
        try:
            while b"\n" not in self._buf:
                self._fill()
        finally:
            if timeout is not None:
                self.sock.settimeout(self.timeout)
        line, self._buf = self._buf.split(b"\n", 1)
        return line.rstrip(b"\r").decode("latin-1")

    def readexact(self, n):
        while len(self._buf) < n:
            self._fill()
        data, self._buf = self._buf[:n], self._buf[n:]
        return data

    def query(self, line, timeout=None):
        self.send(line)
        r = self.readline(timeout)
        while r.strip() == "done" and getattr(self, "_seq_pending", 0) > 0:  # a clickSeq sent with wait=False
            self._seq_pending -= 1
            r = self.readline(timeout)
        return r

    # --- information ------------------------------------------------------------------------------------------
    def version(self):
        return self.query("getVersion")

    def title_id(self):
        return self.query("getTitleID")

    def title_version(self):
        return self.query("getTitleVersion")

    def build_id(self):
        return self.query("getBuildID")

    def heap_base(self):
        return self.query("getHeapBase")

    def main_nso_base(self):
        return self.query("getMainNsoBase")

    def is_program_running(self, program_id):
        return self.query(f"isProgramRunning {program_id}").strip() == "1"

    def game(self, what="name"):
        return self.query(f"game {what}")

    def charge(self):
        return self.query("charge")

    def app_info(self):
        """Everything sys-botbase can say about the running application (no reply means none is running)."""
        info = {"title_id": self.title_id()}
        tid = info["title_id"].strip()
        if tid and set(tid) != {"0"}:
            for key, fn in (("title_version", self.title_version), ("build_id", self.build_id),
                            ("main_nso_base", self.main_nso_base), ("heap_base", self.heap_base)):
                info[key] = fn()
            for what in ("name", "author", "version"):
                info[what] = self.game(what)
        return info

    # --- input ------------------------------------------------------------------------------------------------
    def press(self, name):
        b = button(name)
        self.used_input = True
        self.send(f"press {b}")
        self.pressed.add(b)

    def release(self, name):
        b = button(name)
        self.send(f"release {b}")
        self.pressed.discard(b)

    def click(self, name):
        self.used_input = True
        self.send(f"click {button(name)}")

    def stick(self, side, x, y):
        side = side.upper()
        if side not in ("LEFT", "RIGHT"):
            raise ValueError("stick side is LEFT or RIGHT")
        x = max(-0x8000, min(0x7FFF, int(x)))
        y = max(-0x8000, min(0x7FFF, int(y)))
        self.used_input = True
        self.send(f"setStick {side} {x} {y}")

    click_ms = 50  # sys-botbase's buttonClickSleepTime (default 50 ms); configure() keeps it in step

    def seq_duration(self, seq):
        """Seconds clickSeq needs: every W<ms>, plus click_ms for each button click."""
        total = 0.0
        for tok in seq.split(","):
            tok = tok.strip()
            if tok.startswith("W") and tok[1:].isdigit():
                total += int(tok[1:]) / 1000.0
            elif tok and tok[0] not in "%&+-":
                total += self.click_ms / 1000.0 + 0.02
        return total

    def click_seq(self, seq, wait=True):
        """Runs a sys-botbase clickSeq. With wait (default) returns once it has run: closing the connection earlier
        would send clickCancel (release_all) and cut the sequence short."""
        if " " in seq:
            raise ValueError("clickSeq takes one string without spaces")
        # main.c: "some params don't parse correctly, such as DDOWN so use the alt": the D-pad's short names in sequences
        short = {"DDOWN": "DD", "DUP": "DU", "DLEFT": "DL", "DRIGHT": "DR"}
        seq = ",".join((tok[0] + short.get(tok[1:], tok[1:])) if tok[:1] in "+-" else short.get(tok, tok)
                       for tok in seq.split(","))
        self.used_input = True
        self.send(f"clickSeq {seq}")
        if wait:
            # sys-botbase's click thread printf()s "done" when the sequence has run, but stdout is only flushed when the
            # next command arrives (main.c), so it would turn up as the reply of whatever comes next (2026-11-05:
            # pixelPeek got "done"). Wait the sequence's length, then sync: getVersion, skipping "done" lines.
            time.sleep(self.seq_duration(seq) + 0.3)
            self.sync()
        else:
            self._seq_pending = getattr(self, "_seq_pending", 0) + 1

    def sync(self):
        """Sends getVersion and reads up to its reply, dropping stray "done" lines of earlier click sequences."""
        self.send("getVersion")
        for _ in range(8):
            line = self.readline(timeout=15.0).strip()
            if line and line != "done":
                return line
        raise BotBaseError("sync: no getVersion reply")

    def ping(self, n=5):
        """Round-trip times of getVersion in ms."""
        out = []
        for _ in range(n):
            t = time.perf_counter()
            self.version()
            out.append((time.perf_counter() - t) * 1000.0)
        return out

    def release_all(self):
        """Every button we pressed, plus both sticks centred and any running click sequence stopped."""
        if self.sock is None:
            return
        # ffxiii2-nx (2026-11-06, the user's "ghost inputs"): only a connection that sent input releases it. Any input
        # command (setStick too) makes sys-botbase attach its virtual Pro Controller, so the unconditional setStick
        # pair here attached a controller on every file transfer, status query and screenshot. switch.py detach (at
        # the end of every automated test) takes it off again.
        if not getattr(self, "used_input", False):
            return
        self.send("clickCancel")
        for b in list(self.pressed) or []:
            self.send(f"release {b}")
        self.pressed.clear()
        self.send("setStick LEFT 0 0")
        self.send("setStick RIGHT 0 0")

    def configure(self, key, value):
        self.send(f"configure {key} {value}")
        if key == "buttonClickSleepTime":
            self.click_ms = int(value)

    def detach_controller(self):
        """Takes sys-botbase's virtual Pro Controller off the console (it attaches itself on the next input command)."""
        self.send("detachController")

    # --- screen -----------------------------------------------------------------------------------------------
    def screenshot(self, timeout=30.0):
        """The current screen as JPG bytes (pixelPeek; the HOME menu and games, not every applet)."""
        line = self.query("pixelPeek", timeout=timeout).strip()
        try:
            data = bytes.fromhex(line)
        except ValueError:
            # ffxiii2-nx (2026-11-06, k204 GC): a click sequence's late "done" (sys-botbase flushes its stdout with the
            # next reply) landed inside the hex line. Drop it; else resync and ask once more.
            cleaned = line.replace("done", "")
            try:
                data = bytes.fromhex(cleaned)
            except ValueError:
                self.sync()
                line = self.query("pixelPeek", timeout=timeout).strip()
                try:
                    data = bytes.fromhex(line.replace("done", ""))
                except ValueError:
                    raise BotBaseError(f"pixelPeek: {line[:80]!r}")
        if not data.startswith(b"\xff\xd8"):
            raise BotBaseError("pixelPeek did not return a JPG")
        return data

    # --- memory -----------------------------------------------------------------------------------------------
    def peek(self, addr, size, base="heap"):
        cmd = {"heap": "peek", "main": "peekMain", "absolute": "peekAbsolute"}[base]
        return bytes.fromhex(self.query(f"{cmd} 0x{addr:X} {size}").strip())

    # --- application ------------------------------------------------------------------------------------------
    def kill_game(self):
        return self.query("killGame").strip()

    # --- SD card files ----------------------------------------------------------------------------------------
    def file_list(self, path):
        self.send(f"fileList {check_path(path)}")
        out = []
        while True:
            line = self.readline()
            if line == "":
                return out
            if line.startswith("err"):
                raise BotBaseError(f"fileList {path}: {line}")
            if line.startswith("D "):
                out.append({"type": "dir", "name": line[2:], "size": None})
            elif line.startswith("F "):
                size, name = line[2:].split(" ", 1)
                out.append({"type": "file", "name": name, "size": int(size)})

    def file_size(self, path):
        r = self.query(f"fileSize {check_path(path)}").strip()
        if r.startswith("err"):
            raise BotBaseError(f"fileSize {path}: {r}")
        return int(r)

    def file_read(self, path, offset=0, length=0, timeout=120.0):
        self.send(f"fileRead {check_path(path)} {offset} {length}")
        head = self.readline(timeout).strip()
        if head.startswith("err") or head == "-1":
            raise BotBaseError(f"fileRead {path}: {head}")
        n = int(head)
        old = self.sock.gettimeout()
        self.sock.settimeout(timeout)
        try:
            return self.readexact(n)
        finally:
            self.sock.settimeout(old)

    def _put(self, cmd, path, data, timeout):
        self.send(f"{cmd} {check_path(path)} {len(data)}", data)
        r = self.readline(timeout).strip()
        if not r.startswith("ok"):
            raise BotBaseError(f"{cmd} {path}: {r}")
        written = int(r.split()[1]) if len(r.split()) > 1 else len(data)
        if written != len(data):
            raise BotBaseError(f"{cmd} {path}: {written} of {len(data)} bytes written")

    def file_write(self, path, data, progress=None, timeout=120.0):
        """Creates / overwrites path; data goes in 1 MiB calls (fileWrite, then fileAppend)."""
        if not data:
            self._put("fileWrite", path, b"", timeout)
            return
        for at in range(0, len(data), CHUNK):
            self._put("fileWrite" if at == 0 else "fileAppend", path, data[at:at + CHUNK], timeout)
            if progress:
                progress(min(at + CHUNK, len(data)), len(data))

    def file_append(self, path, data, timeout=120.0):
        self._put("fileAppend", path, data, timeout)

    def _ok(self, line, what):
        r = self.query(line).strip()
        if r != "ok":
            raise BotBaseError(f"{what}: {r}")

    def file_delete(self, path):
        self._ok(f"fileDelete {check_path(path)}", f"fileDelete {path}")

    def file_mkdir(self, path):
        self._ok(f"fileMkdir {check_path(path)}", f"fileMkdir {path}")

    def file_rename(self, src, dst):
        self._ok(f"fileRename {check_path(src)} {check_path(dst)}", f"fileRename {src} {dst}")


def connect(cfg):
    """Connects and sets the press length: the game reads the pad once a frame, and at 8-30 fps a 50 ms click
    (sys-botbase's default) can fall between two reads (seen 2026-11-05: A on "Press any button" ignored)."""
    b = BotBase(cfg["switch_ip"], int(cfg.get("botbase_port", 6000)), float(cfg.get("timeout_s", 10.0))).connect()
    click_ms = int(cfg.get("click_ms", 200))
    if click_ms != 50:
        b.configure("buttonClickSleepTime", click_ms)
    return b


def wait_for(host, port=6000, total_s=120.0, step_s=3.0):
    """Polls until sys-botbase answers getVersion (after a reboot); returns the version or None."""
    end = time.time() + total_s
    while time.time() < end:
        try:
            with BotBase(host, port, 5.0) as b:
                return b.version()
        except OSError:
            time.sleep(step_s)
    return None
