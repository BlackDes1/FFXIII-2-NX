# Switch Frame Tap in the ffxiii2-nx workflow

Switch Frame Tap (https://github.com/TomasUribe/switch-frame-tap, GPL-2.0, by TomasUribe) is an Atmosphère sysmodule
that captures the running game's frames (VIC copy of the presented buffer, NVENC H.264) and sends them to a PC viewer
over USB or the network, and saves native-resolution PNG screenshots on a button combo. Here it is an ADDITIONAL
visual-debugging capability; it does not replace the profiler overlay, sys-botbase screenshots or the logs, and it is
never part of a performance measurement.

## Installed version

| item | value |
|---|---|
| release | v0.7.21.1 (marked Latest, 2026-11-03; no -beta tag) |
| Switch package | `switch-frame-tap-0.7.21.1-switch.zip`, SHA-256 `E8555B0A2FDA1C5E5F222E2D90F620641DBAFC27C5F7CA2B65B8904198E78866` |
| Windows viewer | `switch-frame-tap-0.7.21.1-windows-viewer.zip`, SHA-256 `3F4F601701A3B5A98A5D15E20B3CF644505D9885C2FDB377567344A311226AD6` |
| local copies | `Works\_xfer\switch-frame-tap-0.7.21.1\` |
| installed | 2026-11-06 05:38 by `tools/switch_remote/frametap_sd.py install` (27 files, each SHA-256 verified on the card) |

Files on the SD card:

- `atmosphere/contents/0100000000000C20/exefs.nsp` - applet-mitm, the capture module (started by the launcher)
- `atmosphere/contents/0100000000000C21/` - launcher / memory donor, `flags/boot2.flag`, `toolbox.json`
- `switch/switch-frame-tap/switch-frame-tap.nro` - the manager app
- `switch/.overlays/switch-frame-tap.ovl` - the Ultrahand/Tesla overlay
- `config/switch-frame-tap/dvr-patches/` - optional sound patches for no-record games (off by default)
- `config/switch-frame-tap/config.ini` - written by us (see Settings)

## Compatibility check (2026-11-06)

- Firmware: Frame Tap streams on 21.x-23.x only and refuses others unless `allow_untested_firmware = 1` (kept 0).
  It writes the firmware and Atmosphère versions to `log.txt` at boot - copy them here after the first boot.
- Atmosphère: >= 1.11.2 needed by 0.7.7+ (older gave a fatal at boot, fixed). Version: see log.txt.
- Sysmodules at boot on this console: SaltyNX, hoc-clk (Horizon OC), sys-patch, nx-ovlloader, sys-botbase,
  sys-ftpd, `00FF004D454D54FF` (no toolbox.json). No SysDVR (which would own the USB gadget). No other module
  intercepts vi / dispdrv / audout / appletOE.
- Memory: the capture buffers come from the launcher's donation out of the Applet pool, not from the application
  pool the game uses. If the system pool is too small, Frame Tap does not start and the console boots normally.
- Our NRO runs as the application (title override) and presents through vi:m: that is the path Frame Tap wraps
  (`ViManagerRootMitm`, application program ids), so the game is captured.
- emuMMC is present (`/emuMMC/RAW1`, `RAW2`). Frame Tap does not touch NAND or the bootloader. No NAND backup was
  found on the SD card (`/backup` empty): the user was asked whether one exists on the PC.
- SD config backups: `Works\switch_sd_backup\<time>\` (`/config`, `/atmosphere/config`, hosts, bootloader ini, emummc.ini,
  each contents folder's toolbox.json + flags, manifest.json with SHA-256).

## Settings (`sdmc:/config/switch-frame-tap/config.ini`)

Changed only with `frametap_sd.py config --set k=v`, which first copies the file to `config.ini.<time>.bak`.

| key | value | why |
|---|---|---|
| memory_boost | 0 | stock RAM clock; the RAM-1600 path was removed from the port on purpose |
| homebrew_docked | 0 | handheld only; must not change the game's resolution choice |
| max_resolution / scale_to_1080 | 720 / 0 | handheld native |
| quality | high | best picture for defect hunting |
| screenshot / screenshot_buttons | 1 / 2 (ZL+ZR+D-pad Down) | L3+R3 is our profiler toggle; ZL+ZR+Down is pressed through sys-botbase |
| screenshot_location | app | PNGs in `sdmc:/switch/switch-frame-tap/screenshots/` |
| connection | usb | network mode needs the Windows private-network prompt (user decision) |
| allow_untested_firmware | 0 | safety |
| home_menu, overlays, viewer_input, update_betas | 0 | not needed; betas never |

No overclock, ReverseNX-RT or SaltyNX change is made for capture.

## Two kinds of session

| | visual | perf |
|---|---|---|
| Frame Tap | on (screenshots, optional stream/recording) | sysmodule off: manager -> Start with the console = off, reboot |
| what it is for | textures, shaders, lighting, flicker bands, resolution, A/B pictures | fps, GPU spans, frame times on the A57 at 1020 MHz |
| `vd.py new --mode` | visual (session.json: benchmark_valid = false) | perf |

Frame Tap wraps the game's display service and binder (every queue/dequeue goes through it), runs an input thread
every 25 ms and VIC copies while a viewer reads. Numbers taken while it runs are not compared with perf numbers.

## PC viewer (Windows)

Unzip `switch-frame-tap-0.7.21.1-windows-viewer.zip` to a writable folder, keep the DLLs beside `SwitchFrameTap.exe`.

- USB (handheld only, USB-A on the PC side): Zadig -> Options -> List All Devices -> "Switch Frame Tap" (1209 5F1E)
  -> WinUSB. A driver install is a system change: done by the user, for that device only.
- Network: manager -> Connection = Network, restart the console; viewer Tab until "Connection: Network", I = type the
  address (<console IP>). Stream TCP 9950, discovery UDP 9951. `SwitchFrameTap.exe --check --net <console IP>`
  tests the path. Windows asks once to allow private networks. Never forward these ports or allow public networks.
- Keys: F11 fullscreen, O/F3 stats overlay, R record MP4 (to `Videos\Switch Frame Tap`, needs 1 GB free), Tab USB/
  network, I address, P picture controls, V vsync, M mute, Esc settings.
- Viewer log: `%APPDATA%\switch-frame-tap\viewer\viewer.log`.

## Logs

`python tools/switch_remote/frametap_sd.py logs` copies `log.txt`, `log.prev.txt`, `last.txt`, `launcher.txt` and the
newest `atmosphere/crash_reports` to `logs/frametap/<time>/`. Read these before judging any picture.

## Visual debugging tools (`tools/visual_debug/vd.py`)

Sessions live under `logs/visual/<build_id>/<test_session>/` (`shots/`, `recordings/`, `frames/`, `diffs/`,
`session.json`).

```
python tools/visual_debug/vd.py new k198 battle_flicker --toml renut_dev.toml --mode visual
python tools/visual_debug/vd.py shoot logs/visual/k198/battle_flicker --count 30 --every 2 --label battle
python tools/visual_debug/vd.py recordings logs/visual/k198/battle_flicker
python tools/visual_debug/vd.py frames <mp4> <out_dir> --every 0.5          (ffmpeg: run in the Linux VM)
python tools/visual_debug/vd.py compare a.png b.png <out_dir> --threshold 16 [--rows 300:420]
python tools/visual_debug/vd.py builds logs/visual/k197/battle logs/visual/k198/battle
python tools/visual_debug/vd.py index logs/visual/k198/battle_flicker
```

`compare` reports per-channel mean/max difference, the share of pixels over the threshold, the changed bounding box,
the 16-row band with most change and the first/last mostly-changed rows (a predicated-tile seam such as y 336 shows
up there), and writes a heatmap and an A | B | heatmap picture. `builds` pairs two sessions' shots by label (else by
index). Pictures are evidence next to the logs, never a verdict on their own.

## Things learned on the console (2026-11-06)

- Our deko3d swapchain images are hardware-compressed: Frame Tap's VIC copy then reads black. Visual sessions use
  `ffxiii2_swapchain_compression = false` (renut_frametap*.toml); same pixels, never a benchmark.
- USB recording without the window: `SwitchFrameTap.exe --headless --mp4 NAME.mp4 --seconds N`, started with the
  output folder as working directory (the job runner drops double quotes, so no spaces in the argument). It records
  every game frame (87 s = 2,136 frames at ~23 fps).
- Frame Tap's anti-debug heuristic: an application that closes within ~1.5 s of a stream attaching is put on
  `config/switch-frame-tap/excluded.txt` and never captured again. Our kill right after a recording triggered it
  (BUGS.md D-005). Wait 20 s after the viewer ends before closing the game; to undo, take the id out of excluded.txt
  (read at boot) or use "Stream this app" in the overlay.
- Frame Tap's toast "Screenshot saved" shows in sys-botbase's captures, not in its own PNGs.
- The screenshot combo (ZL+ZR+D-pad Down) also reaches the game (moves the battle menu cursor).
- `tools/visual_debug/band_scan.py` counts the smoke band per frame of a recording; `--stamp` reads the frame number
  `ffxiii2_diag_frame_stamp` writes (diagnostic, changes the corner of the picture).

## Recovery

Manager -> Start with the console = off and reboot; or delete `atmosphere/contents/0100000000000C20` and `...C21`
(PC card reader); or hold Volume Up at boot so Atmosphère skips contents sysmodules.

## Verification status

| check | status |
|---|---|
| files on SD, SHA-256 verified | done 2026-11-06 05:38 |
| sysmodule starts (log.txt) | waiting for the reboot |
| game captured, screenshot via combo | waiting for the reboot |
| viewer connects, stream stable, recording | waiting for the user's USB-driver / firewall choice |
| file retrieval, comparisons | tools ready (compare tested on a synthetic band image) |

Credits: Switch Frame Tap by TomasUribe (GPL-2.0); our tools only drive it.
