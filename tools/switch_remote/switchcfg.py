"""ffxiii2-nx live Switch testing (2026-11-05): the central configuration.

The configuration is a JSON file OUTSIDE the source tree, because it holds the FTP password:

  1. --config <file> on any tool's command line
  2. the environment variable FFXIII2_SWITCH_CONFIG
  3. <Works>/switch_test_config.json (the folder above the repository; the default on the user's PC)
  4. <repository>/switch_test_config.json (ignored by git, see .gitignore)

tools/switch_remote/switch_test_config.example.json shows every key. Never commit a filled-in copy.
"""
import json
import os

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WORKS = os.path.dirname(REPO)

DEFAULTS = {
    "switch_ip": "",                 # the console's LAN address (System Settings > Internet > Connection status)
    "botbase_port": 6000,             # sys-botbase (masagrator fork 2.6-files-kill)
    "ftp_port": 5000,                 # sys-ftpd-light (Thorhax 1.0.5-thorhax1)
    "ftp_user": "",
    "ftp_password": "",
    "nro_dir": "/switch/ffxiii2-nx",  # folder of ffxiii2.nro on the SD card
    "log_dir": "/switch/ffxiii2-nx/logs",
    "project": "ffxiii2-nx",
    "nro_name": "ffxiii2.nro",
    "timeout_s": 10.0,
    "retries": 3,
    # clickSeq strings (sys-botbase syntax) that start the NRO from the HOME menu after killGame. They depend on how
    # the console launches homebrew (title override with R held, album applet, hbmenu forwarder) and on where the
    # entry sits in hbmenu, so they must be recorded on the user's console first (docs/LIVE_SWITCH_TESTING.md, 9).
    "launch_sequence": [],
    "launch_wait_s": 90,
    "local_builds_dir": os.path.join(WORKS, "switch_builds"),
    "local_logs_dir": os.path.join(REPO, "logs", "switch"),
}


def config_path(explicit=None):
    for p in (explicit, os.environ.get("FFXIII2_SWITCH_CONFIG"),
              os.path.join(WORKS, "switch_test_config.json"), os.path.join(REPO, "switch_test_config.json")):
        if p and os.path.isfile(p):
            return p
    return None


def load(explicit=None, need_ip=True):
    cfg = dict(DEFAULTS)
    path = config_path(explicit)
    if path:
        with open(path, encoding="utf-8") as f:
            cfg.update(json.load(f))
        cfg["_path"] = path
    else:
        cfg["_path"] = None
    if os.environ.get("FFXIII2_SWITCH_IP"):
        cfg["switch_ip"] = os.environ["FFXIII2_SWITCH_IP"]
    if need_ip and not cfg["switch_ip"]:
        raise SystemExit("switch_ip is not set: run tools/switch_remote/switch.py discover, then put the address into "
                         + (path or os.path.join(WORKS, "switch_test_config.json")))
    return cfg


def save(cfg, path=None):
    path = path or cfg.get("_path") or os.path.join(WORKS, "switch_test_config.json")
    data = {k: v for k, v in cfg.items() if not k.startswith("_")}
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
        f.write("\n")
    return path


def add_common_args(ap):
    ap.add_argument("--config", help="switch_test_config.json (default: see switchcfg.py)")
    ap.add_argument("--ip", help="console address, overrides the config")
    ap.add_argument("--transport", choices=["auto", "botbase", "ftp"], default="auto",
                    help="file transport: sys-botbase file commands (default when reachable) or FTP")


def from_args(a, need_ip=True):
    cfg = load(getattr(a, "config", None), need_ip=False)
    if getattr(a, "ip", None):
        cfg["switch_ip"] = a.ip
    if need_ip and not cfg["switch_ip"]:
        load(getattr(a, "config", None), need_ip=True)
    return cfg
