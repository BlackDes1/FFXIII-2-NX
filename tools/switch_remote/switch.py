#!/usr/bin/env python3
"""ffxiii2-nx: remote control of the console (sys-botbase 2.6-files-kill on port 6000, sys-ftpd-light on 5000).

  python tools/switch_remote/switch.py discover            find consoles answering sys-botbase on the local /24 networks
  python tools/switch_remote/switch.py status              version, battery, running application, SD folder, FTP check
  python tools/switch_remote/switch.py ls /switch/ffxiii2-nx/logs
  python tools/switch_remote/switch.py get /switch/ffxiii2-nx/logs/renut_013.log [local]
  python tools/switch_remote/switch.py put local.bin /switch/ffxiii2-nx/x.bin   (via .part, size + SHA-256 checked)
  python tools/switch_remote/switch.py mkdir|rm|mv ...
  python tools/switch_remote/switch.py shot [file.jpg]     screenshot (pixelPeek)
  python tools/switch_remote/switch.py click A | press A | release A | stick LEFT 0 32767 | seq A,W500,B | release-all
  python tools/switch_remote/switch.py kill                close the running application (killGame)
  python tools/switch_remote/switch.py peek main 0x1000 64

Every command takes --config / --ip / --transport (see switchcfg.py). The exit code is 0 on success and 2 when the
console could not be reached, so jobs can tell a missing console from a failed command.
"""
import argparse
import concurrent.futures
import ipaddress
import json
import os
import socket
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from switch_remote import botbase, sdfiles, switchcfg, switchftp  # noqa: E402


def local_networks():
    addrs = set()
    try:
        for a in socket.gethostbyname_ex(socket.gethostname())[2]:
            addrs.add(a)
    except OSError:
        pass
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("192.0.2.1", 9))  # no packet is sent: only picks the outgoing interface
        addrs.add(s.getsockname()[0])
        s.close()
    except OSError:
        pass
    nets = set()
    for a in addrs:
        ip = ipaddress.ip_address(a)
        if ip.is_private and not ip.is_loopback and not ip.is_link_local:
            nets.add(ipaddress.ip_network(f"{a}/24", strict=False))
    return sorted(nets, key=str)


def probe(ip, port, timeout):
    try:
        with socket.create_connection((str(ip), port), timeout=timeout) as s:
            s.settimeout(timeout * 4)
            s.sendall(b"getVersion\r\n")
            data = b""
            while b"\n" not in data and len(data) < 256:
                chunk = s.recv(256)
                if not chunk:
                    break
                data += chunk
            return str(ip), data.split(b"\n")[0].decode("latin-1").strip()
    except OSError:
        return None


def cmd_discover(a, cfg):
    nets = [ipaddress.ip_network(n) for n in a.net] if a.net else local_networks()
    if not nets:
        print("no private IPv4 network found on this machine")
        return 2
    found = []
    port = int(cfg.get("botbase_port", 6000))
    for net in nets:
        print(f"scanning {net} port {port} ...", flush=True)
        with concurrent.futures.ThreadPoolExecutor(64) as ex:
            for r in ex.map(lambda ip: probe(ip, port, a.timeout), net.hosts()):
                if r:
                    found.append(r)
                    print(f"  {r[0]}: sys-botbase {r[1]!r}", flush=True)
    if not found:
        print("no console answered (is sys-botbase installed, the console booted into CFW and on this network?)")
        return 2
    if a.save and len(found) == 1:
        cfg["switch_ip"] = found[0][0]
        print("saved to", switchcfg.save(cfg))
    return 0


def cmd_status(a, cfg):
    out = {"config": cfg.get("_path"), "switch_ip": cfg["switch_ip"]}
    rc = 0
    try:
        with botbase.connect(cfg) as b:
            out["botbase_version"] = b.version()
            out["botbase_version_ok"] = out["botbase_version"] == botbase.EXPECTED_VERSION
            out["battery_percent"] = b.charge()
            out["application"] = b.app_info()
            try:
                out["nro_dir"] = b.file_list(cfg["nro_dir"])
            except botbase.BotBaseError as e:
                out["nro_dir"] = f"error: {e}"
    except OSError as e:
        out["botbase"] = f"unreachable: {e}"
        rc = 2
    try:
        with switchftp.SwitchFTP(cfg["switch_ip"], int(cfg["ftp_port"]), cfg["ftp_user"], cfg["ftp_password"],
                                 timeout=8.0, retries=2) as f:
            out["ftp_root"] = [e["name"] for e in f.list("/")]
    except Exception as e:  # noqa: BLE001
        out["ftp"] = f"not usable: {e}"
    print(json.dumps(out, indent=2))
    return rc


def cmd_files(a, cfg):
    with sdfiles.open_sd(cfg, a.transport) as sd:
        print(f"[transport: {sd.kind}]", file=sys.stderr)
        if a.cmd == "ls":
            for e in sd.list(a.path):
                print(("D " if e["type"] == "dir" else f"F {e['size']:>12} ") + e["name"])
        elif a.cmd == "get":
            data = sd.read(a.path)
            remote = sd.size(a.path)
            dst = a.local or os.path.basename(a.path)
            with open(dst, "wb") as f:
                f.write(data)
            note = "" if len(data) == remote else f" (file is now {remote} bytes: it grew while being read)"
            print(f"{a.path} -> {dst}: {len(data)} bytes{note}")
        elif a.cmd == "put":
            data = open(a.local, "rb").read()
            t = time.time()
            sd.put(a.path, data, verify=not a.no_verify,
                   progress=lambda d, n: print(f"\r  {d >> 20}/{n >> 20} MiB", end="", file=sys.stderr))
            print(f"\n{a.local} -> {a.path}: {len(data)} bytes in {time.time() - t:.1f} s, verified "
                  f"{'size' if a.no_verify else 'size + SHA-256'}")
        elif a.cmd == "mkdir":
            sd.mkdirs(a.path)
        elif a.cmd == "rm":
            sd.delete(a.path)
        elif a.cmd == "mv":
            sd.rename(a.path, a.dst)
    return 0


def cmd_input(a, cfg):
    with botbase.connect(cfg) as b:
        # ffxiii2-nx (2026-11-06): --if-title: press only while that application runs (a loop of presses must not go on
        # on HOME after the game closed: k205's A loop restarted the game from HOME twice)
        want = getattr(a, "if_title", None)
        if want:
            tid = b.title_id().strip().upper()
            if tid != want.upper():
                print(f"not pressing: the running application is {tid or 'none'}")
                return 7
        if a.cmd == "click":
            for name in a.buttons:
                b.click(name)
                time.sleep(a.gap)
        elif a.cmd == "press":
            for name in a.buttons:
                b.press(name)
            b.pressed.clear()  # stays held on purpose until "release"
        elif a.cmd == "release":
            for name in a.buttons:
                b.release(name)
        elif a.cmd == "stick":
            b.stick(a.side, int(a.x, 0), int(a.y, 0))
            time.sleep(a.hold)
            if a.hold:
                b.stick(a.side, 0, 0)
        elif a.cmd == "seq":
            if a.click_ms:
                b.configure("buttonClickSleepTime", a.click_ms)
            b.click_seq(a.seq)
        elif a.cmd == "release-all":
            for name in botbase.BUTTONS:
                b.send(f"release {name}")
            b.send("setStick LEFT 0 0")
            b.send("setStick RIGHT 0 0")
        elif a.cmd == "detach":
            # ffxiii2-nx (2026-11-06, the user's "ghost inputs"): every button released, sticks centred, then
            # sys-botbase's virtual Pro Controller taken off the console: the end of every automated test.
            b.send("clickCancel")
            for name in botbase.BUTTONS:
                b.send(f"release {name}")
            b.send("setStick LEFT 0 0")
            b.send("setStick RIGHT 0 0")
            time.sleep(0.2)
            b.detach_controller()
        time.sleep(0.1)
    return 0


def cmd_shot(a, cfg):
    with botbase.connect(cfg) as b:
        data = b.screenshot()
    dst = a.file or time.strftime("switch_%Y%m%d_%H%M%S.jpg")
    with open(dst, "wb") as f:
        f.write(data)
    print(f"{dst}: {len(data)} bytes")
    return 0


def cmd_ping(a, cfg):
    t = time.perf_counter()
    b = botbase.connect(cfg)
    conn = (time.perf_counter() - t) * 1000.0
    rt = b.ping(a.n)
    t = time.perf_counter()
    b.close()
    print(f"connect {conn:.0f} ms; getVersion round trips {', '.join(f'{x:.0f}' for x in rt)} ms; "
          f"close {(time.perf_counter() - t) * 1000.0:.0f} ms")
    return 0


def cmd_kill(a, cfg):
    with botbase.connect(cfg) as b:
        print(b.kill_game())
    return 0


def cmd_peek(a, cfg):
    with botbase.connect(cfg) as b:
        print(b.peek(int(a.addr, 16), int(a.size, 0), a.base).hex())
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    switchcfg.add_common_args(ap)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("discover")
    p.add_argument("--net", action="append", help="network to scan, e.g. 192.168.1.0/24 (default: this PC's)")
    p.add_argument("--timeout", type=float, default=0.4)
    p.add_argument("--save", action="store_true", help="write the address into the config if exactly one answers")
    sub.add_parser("status")
    for name in ("ls", "mkdir", "rm"):
        sub.add_parser(name).add_argument("path")
    p = sub.add_parser("get")
    p.add_argument("path")
    p.add_argument("local", nargs="?")
    p = sub.add_parser("put")
    p.add_argument("local")
    p.add_argument("path")
    p.add_argument("--no-verify", action="store_true", help="size check only, no read-back")
    p = sub.add_parser("mv")
    p.add_argument("path")
    p.add_argument("dst")
    for name in ("click", "press", "release"):
        p = sub.add_parser(name)
        p.add_argument("buttons", nargs="+")
        p.add_argument("--gap", type=float, default=0.25)
        p.add_argument("--if-title", help="press only while this title id is the running application")
    p = sub.add_parser("stick")
    p.add_argument("side")
    p.add_argument("x")
    p.add_argument("y")
    p.add_argument("--hold", type=float, default=0.5, help="seconds, then centre (0 = leave it)")
    p = sub.add_parser("seq")
    p.add_argument("seq")
    p.add_argument("--click-ms", type=int, default=0, help="buttonClickSleepTime for this sequence (default: keep)")
    p.add_argument("--if-title", help="press only while this title id is the running application")
    p = sub.add_parser("ping")
    p.add_argument("-n", type=int, default=5)
    sub.add_parser("release-all")
    sub.add_parser("detach")
    sub.add_parser("shot").add_argument("file", nargs="?")
    sub.add_parser("kill")
    p = sub.add_parser("peek")
    p.add_argument("base", choices=["heap", "main", "absolute"])
    p.add_argument("addr")
    p.add_argument("size")
    a = ap.parse_args()
    cfg = switchcfg.from_args(a, need_ip=a.cmd != "discover")
    fn = {"discover": cmd_discover, "status": cmd_status, "shot": cmd_shot, "kill": cmd_kill, "ping": cmd_ping, "peek": cmd_peek}.get(a.cmd)
    if fn is None:
        fn = cmd_files if a.cmd in ("ls", "get", "put", "mkdir", "rm", "mv") else cmd_input
    try:
        return fn(a, cfg)
    except (OSError, sdfiles.SdError) as e:
        print(f"console not reachable at {cfg.get('switch_ip')}: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
