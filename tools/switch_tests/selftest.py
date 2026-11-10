#!/usr/bin/env python3
"""ffxiii2-nx: tests every live-Switch tool against mock_botbase.py (a fake SD card in a temporary folder), and the
FTP client against pyftpdlib when that is installed. No console, no network beyond 127.0.0.1.

  python tools/switch_tests/selftest.py [--keep]
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
sys.path.insert(0, TOOLS)
sys.path.insert(0, HERE)
import mock_botbase  # noqa: E402

FAILS = []


def check(cond, what):
    print(("  ok   " if cond else "  FAIL ") + what, flush=True)
    if not cond:
        FAILS.append(what)


def run(cfg_path, *args):
    env = dict(os.environ, FFXIII2_SWITCH_CONFIG=cfg_path)
    r = subprocess.run([sys.executable, *args], capture_output=True, text=True, env=env, timeout=300)
    return r.returncode, r.stdout + r.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    tmp = tempfile.mkdtemp(prefix="ffxiii2_selftest_")
    sd = os.path.join(tmp, "sd")
    nro_dir = os.path.join(sd, "switch", "ffxiii2-nx")
    os.makedirs(os.path.join(nro_dir, "logs", "rex"))
    os.makedirs(os.path.join(nro_dir, "renut", "SAVE"))
    for name, data in (("ffxiii2.nro", b"OLD-NRO" * 1000), ("ffxiii2.toml", b"a = 1\n"),
                       ("deko3d_shaders.bin", os.urandom(3 << 20)), ("renut/SAVE/ff13-00.dat", b"save")):
        with open(os.path.join(nro_dir, name.replace("/", os.sep)), "wb") as f:
            f.write(data)
    log = ("[2026-11-05 12:24:00.951] [info] [core] [t1] start\n"
           "[2026-11-05 12:24:04.605] [error] [sys] [t2] File not found: game:\\white-extension.xex\n"
           "[2026-11-05 12:24:05.000] [warning] [sys] [t2] Too few processor cores - scheduling will be wonky\n"
           + "".join(f"[2026-11-05 12:25:{i:02d}.000] [info] [core] [t1] [renut] {10 * i}.0 s: 300 game frames "
                     f"({20 + i}.0 fps), render thread busy {30 + i}.0 ms a frame (300 frames), GPU 20.0 ms a frame\n"
                     for i in range(1, 8)))
    with open(os.path.join(nro_dir, "logs", "renut_001.log"), "w") as f:
        f.write(log)
    with open(os.path.join(nro_dir, "logs", "rex", "rex_profile.log"), "w") as f:
        f.write("image 0x0000003443600000\n\nStack sampling: disabled\n==== 10.0 s | mode: normal | game 9.1 fps | CPU "
                "total 53% (400% = 4 cores)\n     per frame: 1400 draws | x\n")
    srv = mock_botbase.start(sd)
    cfg = {"switch_ip": "127.0.0.1", "botbase_port": srv.server_address[1], "ftp_port": 1, "ftp_user": "u",
           "ftp_password": "p", "nro_dir": "/switch/ffxiii2-nx", "log_dir": "/switch/ffxiii2-nx/logs",
           "project": "ffxiii2-nx", "timeout_s": 5, "retries": 1, "launch_sequence": ["HOME,W500,A"], "launch_wait_s": 6,
           "local_builds_dir": os.path.join(tmp, "builds"), "local_logs_dir": os.path.join(tmp, "logs")}
    cfg_path = os.path.join(tmp, "switch_test_config.json")
    json.dump(cfg, open(cfg_path, "w"))
    sw = os.path.join(TOOLS, "switch_remote", "switch.py")
    dep = os.path.join(TOOLS, "switch_deploy", "deploy.py")
    col = os.path.join(TOOLS, "switch_logs", "collect.py")

    print("switch.py")
    rc, out = run(cfg_path, sw, "status")
    check(rc == 0 and '"botbase_version_ok": true' in out, "status: version 2.6-files-kill recognised")
    rc, out = run(cfg_path, sw, "ls", "/switch/ffxiii2-nx")
    check(rc == 0 and "ffxiii2.nro" in out and "D logs" in out, "ls lists files and folders")
    big = os.path.join(tmp, "big.bin")
    open(big, "wb").write(os.urandom((2 << 20) + 12345))
    rc, out = run(cfg_path, sw, "put", big, "/switch/ffxiii2-nx/big.bin")
    check(rc == 0 and "size + SHA-256" in out, "put: 2 MiB in chunks, verified")
    check(open(os.path.join(nro_dir, "big.bin"), "rb").read() == open(big, "rb").read(), "put: bytes identical")
    check(not os.path.exists(os.path.join(nro_dir, "big.bin.part")), "put: no .part left")
    got = os.path.join(tmp, "got.bin")
    rc, out = run(cfg_path, sw, "get", "/switch/ffxiii2-nx/big.bin", got)
    check(rc == 0 and open(got, "rb").read() == open(big, "rb").read(), "get: bytes identical")
    rc, out = run(cfg_path, sw, "shot", os.path.join(tmp, "s.jpg"))
    check(rc == 0 and open(os.path.join(tmp, "s.jpg"), "rb").read()[:2] == b"\xff\xd8", "shot: JPG")
    rc, out = run(cfg_path, sw, "click", "A", "L3", "--gap", "0")
    inputs = open(os.path.join(sd, ".inputs.log")).read()
    check("click A" in inputs and "click LSTICK" in inputs, "click: aliases mapped (L3 -> LSTICK)")
    check(inputs.rstrip().endswith("setStick RIGHT 0 0"), "input released / sticks centred at disconnect")

    print("deploy.py")
    pkg = os.path.join(tmp, "pkg")
    os.makedirs(os.path.join(pkg, "COPY_TO_SD_switch_ffxiii2-nx", "configs"))
    open(os.path.join(pkg, "COPY_TO_SD_switch_ffxiii2-nx", "ffxiii2.nro"), "wb").write(b"NEW-NRO-1" * 999)
    open(os.path.join(pkg, "COPY_TO_SD_switch_ffxiii2-nx", "ffxiii2.toml"), "wb").write(b"a = 1\n")
    shutil.copy(os.path.join(nro_dir, "deko3d_shaders.bin"), os.path.join(pkg, "COPY_TO_SD_switch_ffxiii2-nx"))
    open(os.path.join(pkg, "COPY_TO_SD_switch_ffxiii2-nx", "configs", "renut_ab.toml"), "w").write("x = 1\n")
    open(os.path.join(pkg, "ffxiii2-nx.elf"), "wb").write(b"\x7fELF" + b"0" * 100)
    mock_botbase.State.title = ""
    rc, out = run(cfg_path, dep, "deploy", "--package", pkg, "--name", "t1")
    check(rc == 0, "deploy 1 ran" + ("" if rc == 0 else ": " + out[-400:]))
    check("files to upload: 2 of 4" in out, "deploy 1: no current.json -> live files hashed, only NRO + new config sent")
    check(open(os.path.join(nro_dir, "ffxiii2.nro"), "rb").read() == b"NEW-NRO-1" * 999, "deploy 1: NRO live")
    bak = os.path.join(nro_dir, "deploy", "known_good")
    check(os.path.isdir(bak) and any(os.path.isfile(os.path.join(bak, d, "ffxiii2.nro")) for d in os.listdir(bak)),
          "deploy 1: hand-installed build moved to deploy/known_good/<id>/")
    check(os.path.isfile(os.path.join(nro_dir, "renut", "SAVE", "ff13-00.dat")), "saves untouched")
    open(os.path.join(pkg, "COPY_TO_SD_switch_ffxiii2-nx", "ffxiii2.nro"), "wb").write(b"NEW-NRO-2" * 999)
    rc, out = run(cfg_path, dep, "deploy", "--package", pkg, "--name", "t2", "--slot", "experimental")
    check(rc == 0 and "files to upload: 1 of 4" in out, "deploy 2: incremental, only the NRO uploaded")
    exp = os.path.join(nro_dir, "deploy", "experimental")
    check(os.path.isdir(exp) and len(os.listdir(exp)) == 1, "deploy 2: build 1 backed up to deploy/experimental/")
    mock_botbase.State.title = "0100000000000000"
    rc, out = run(cfg_path, dep, "deploy", "--package", pkg, "--name", "t3")
    check(rc != 0 and "application is running" in out, "deploy refuses while an application runs (no --restart)")
    mock_botbase.State.title = ""
    rc, out = run(cfg_path, dep, "mark-good")
    check(rc == 0 and "known_good" in out, "mark-good")
    rc, out = run(cfg_path, dep, "status")
    check(rc == 0 and "experimental" in out, "status")
    st = json.load(open(os.path.join(tmp, "builds", "state.json")))
    check(st["known_good"] and st["current"], "local state.json has current + known_good")
    check(any(f.endswith(".elf") for d, _, fs in os.walk(os.path.join(tmp, "builds")) for f in fs), "ELF kept on the PC")

    print("collect.py / parse")
    rc, out = run(cfg_path, col, "--session", "t")
    check(rc == 0 and "renut_001.log" in out and "ERRORS" in out and "PERFORMANCE" in out, "collect + report")
    rc, out = run(cfg_path, col, "--session", "t")
    check("no new or changed logs" in out, "collect: unchanged logs are not fetched again")
    with open(os.path.join(nro_dir, "logs", "renut_001.log"), "a") as f:
        f.write("[2026-11-05 12:30:00.000] [error] [core] [t1] Fatal exception in thread X\n")
    rc, out = run(cfg_path, col, "--session", "t")
    check("CRASH" in out and os.path.isdir(os.path.join(tmp, "logs")), "collect: grown log fetched, crash flagged")
    sessions = [d for d, _, fs in os.walk(os.path.join(tmp, "logs")) if "session.json" in fs]
    check(len(sessions) == 2 and any(s.endswith("t_2") for s in sessions), "earlier sessions kept (t, t_2)")

    print("run_scenario.py")
    scen = os.path.join(tmp, "scen.json")
    json.dump({"name": "mini", "description": "selftest", "steps": [
        {"do": "check_running"}, {"do": "shot", "name": "a"}, {"do": "press", "button": "ZR"},
        {"do": "stick", "side": "LEFT", "x": 0, "y": 32767, "s": 0.1}, {"do": "seq", "seq": "A,W100,B"},
        {"do": "wait", "s": 0.2}, {"do": "collect"}]}, open(scen, "w"))
    mock_botbase.State.title = "0100000000000000"
    open(os.path.join(sd, ".inputs.log"), "w").close()
    rc, out = run(cfg_path, os.path.join(HERE, "run_scenario.py"), scen, "--session", "mini")
    check(rc == 0 and "passed" in out, "scenario passed" + ("" if rc == 0 else ": " + out[-300:]))
    inputs = open(os.path.join(sd, ".inputs.log")).read()
    check("release ZR" in inputs.split("press ZR", 1)[-1], "a held button is released at the end")
    mock_botbase.State.title = ""
    json.dump({"name": "mini2", "description": "x", "steps": [{"do": "check_running"}]}, open(scen, "w"))
    rc, out = run(cfg_path, os.path.join(HERE, "run_scenario.py"), scen, "--session", "dead")
    check(rc == 1 and "not running" in out, "termination detected")

    print("compare_builds.py")
    s1 = [s for s in sessions if s.endswith(os.sep + "t")][0]
    rc, out = run(cfg_path, os.path.join(TOOLS, "switch_profiling", "compare_builds.py"), s1, s1)
    check(rc == 0 and "no measurable difference" in out, "compare: same session = no difference")

    print("switchftp.py (mock sys-ftpd, every 3rd data transfer dropped)")
    import mock_ftpd
    ftpd = mock_ftpd.start(sd, "u", "p", flaky=3)
    cfg["ftp_port"] = ftpd.server_address[1]
    cfg["botbase_port"] = 1  # nothing listens: forces the fallback
    cfg["retries"] = 3
    json.dump(cfg, open(cfg_path, "w"))
    rc, out = run(cfg_path, sw, "--transport", "auto", "ls", "/switch/ffxiii2-nx")
    check(rc == 0 and "[transport: ftp]" in out and "ffxiii2.nro" in out, "auto falls back to FTP")
    rc, out = run(cfg_path, sw, "--transport", "ftp", "put", big, "/switch/ffxiii2-nx/big2.bin")
    check(rc == 0 and open(os.path.join(nro_dir, "big2.bin"), "rb").read() == open(big, "rb").read(),
          "FTP upload verified despite dropped transfers" + ("" if rc == 0 else ": " + out[-300:]))
    rc, out = run(cfg_path, col, "--transport", "ftp", "--session", "ftp", "--all")
    check(rc in (0, 1) and "renut_001.log" in out, "collect over FTP")
    cfg["ftp_password"] = "wrong"
    json.dump(cfg, open(cfg_path, "w"))
    rc, out = run(cfg_path, sw, "--transport", "ftp", "ls", "/")
    check(rc != 0, "wrong FTP password refused")
    ftpd.shutdown()

    srv.shutdown()
    print(f"\n{'ALL PASSED' if not FAILS else str(len(FAILS)) + ' FAILED'}  ({tmp})")
    if not a.keep and not FAILS:
        shutil.rmtree(tmp, ignore_errors=True)
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
