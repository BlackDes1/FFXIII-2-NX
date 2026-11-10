# Diagnostic tool status

Last checked 2026-11-06 07:30 (local). Only what was actually run is marked working.

| tool | status | evidence | limits |
|---|---|---|---|
| sys-botbase 2.6-files-kill (port 6000) | working | `switch.py status/ping` (50 ms round trips), input, pixelPeek JPEG, file read/write | no paths with spaces (FTP for those); any input command attaches its virtual Pro Controller: `switch.py detach` ends every automated test (2026-11-06, the ghost-input report) |
| sys-ftpd-light 1.0.5-thorhax1 | working | FTP fallback in `sdfiles.py`, used for paths with spaces | LAN only; password only in `Works\switch_test_config.json` and the SD config |
| Switch Frame Tap 0.7.21.1 (sysmodule) | working | log.txt: Atmosphère 1.12.1, firmware 21.2.0 (supported 21-23), started 33 s after boot; screenshots of the game 1280x720 PNG | our game needs `ffxiii2_swapchain_compression = false` (renut_frametap.toml) or the PNG is black; the combo (ZL+ZR+D-pad Down) also reaches the game (it moved the battle menu cursor) |
| Switch Frame Tap PC viewer (Windows) | USB found | `SwitchFrameTap.exe --check`: "usb: Switch Frame Tap found - open: yes" (WinUSB via Zadig, installed by the user); PnP `USB\VID_1209&PID_5F1E` OK | live view / recordings need the viewer window (keys R, O, F11) |
| L3+R3 profiler overlay (ImGui) | working | renut_dev.toml, console sessions 2026-11-05 | off in release defaults |
| perfil_pilas.flag stack sampler | working | k163 battle profile, symbolised against the matching ELF | costs frame rate: never a benchmark |
| renut GPU profile / [tilelog] / [drawlog] | working | k161-k199 | diagnostics only, keys off by default |
| Ryujinx | available | runs r1-r13 (2026-11-04/05) | not a performance reference |
| `tools/visual_debug/vd.py` | working | k199/k200 sessions under logs/visual/ | PNG read retried while Frame Tap still writes it |
| `tools/diag/log_inventory.py` | working | docs/diagnostics/LOG_INVENTORY.md (1,618 files) | read-only |
