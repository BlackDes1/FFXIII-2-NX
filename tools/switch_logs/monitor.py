#!/usr/bin/env python3
"""ffxiii2-nx: continuous log monitor (polls the console every few seconds; prints new errors, warnings, crashes,
fps periods and anomalies as they appear). Same as collect.py --monitor.

  python tools/switch_logs/monitor.py [--every 5] [--minutes 30] [--session NAME]
"""
import os
import sys

sys.argv.insert(1, "--monitor")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import collect  # noqa: E402

if __name__ == "__main__":
    sys.exit(collect.main())
