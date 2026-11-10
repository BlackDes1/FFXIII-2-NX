# Live Nintendo Switch testing (ffxiii2-nx)

Written 2026-11-05. This adds live console testing to the project. It does not change the plan in CLAUDE.md or
HANDOFF.md: after every change, the console is still the authority for performance. Builds that change the picture
still need the user's decision.

**Status (2026-11-05 ~17:40 UTC+1, verified on the console unless marked)**
- sys-botbase 2.6-kill and sys-ftpd-light 1.0.5-thorhax1 are installed (job k157). The console is at
  **<console IP>**.
  - `getVersion` = `2.6-files-kill`.
  - FTP login, LIST and upload work.
- Transfers:
  - sys-botbase: 69.2 MB NRO uploaded and SHA-256 read back in 515 s (about 270 KB/s for write + read); 510 KB in
    3.5 s.
  - FTP: the same 510 KB took 55.7 s (the known stalls; retries worked).
- Remote start works: `home_launch.py` finds the FF XIII-2 tile by its picture.
- `killGame` and screenshots work.
- First tool deploy: k157, build id `20261105_1701_081c0c675420`. The hand-installed rc4 was moved to
  `deploy/known_good/manual_20261105_1701/`.
- Profiler window: L3+R3 from the remote controller opens it on the console (k157).
- **Logs the game holds open cannot be read while it runs.** The FS refuses the second open: `fileList` size -1,
  `fileRead` -1 (renut_016/017, rex_stderr.log, deko3d.log). `rex_profile.log` (reopened per report) can be read.
  Fix: `log_file_reopen = true` (renut_dev.toml, from k158).
- **The game ignores sys-botbase's controller in handheld mode**: it is player 2, the game reads player 1
  (renut_014). Fix: `input_shared_players = true` (renut_dev.toml, from k158).
- Tools: `tools/switch_tests/selftest.py` (mock sys-botbase including its late `done` reply, and a mock FTP server
  that drops every 3rd transfer) passes on Linux and on the PC's Python.

## 0. Where things run

The cloud workspace and the PC's Linux shell **cannot reach the home network**. Their LAN connections fail with
"Network is unreachable", as tested 2026-11-05. Every tool that talks to the console therefore runs **on the PC**
(Windows, `C:\Python314\python.exe`), started in one of two ways:
- as a line of a job (`Works\ffxiii2-nx\out\job.txt`, run by `run_job.cmd`), or
- by the user in a terminal.

The job's log (`out\job_*.log`) brings the output back. All tools are in the repository under `tools\switch_*`,
standard-library Python only:

| Tool | Purpose |
|---|---|
| `tools/switch_remote/switch.py` | Connection manager / CLI: discover, status, ls/get/put/mkdir/rm/mv, shot, click/press/release/stick/seq, kill, peek |
| `tools/switch_remote/botbase.py` | sys-botbase client (every command of the fork's commands.md, plus file commands and killGame) |
| `tools/switch_remote/switchftp.py` | FTP client (timeouts, retries on a fresh connection, size / SHA-256 checks) |
| `tools/switch_remote/sdfiles.py` | One SD interface: sys-botbase first, FTP fallback; every write goes via `.part` and is verified |
| `tools/switch_remote/install_sysmodules.ps1` | Installs both sysmodules from their release ZIPs onto the card in the PC reader |
| `tools/switch_deploy/deploy.py` | Deploy (incremental), backup, mark-good / mark-failed, rollback, status |
| `tools/switch_logs/collect.py` | Log downloader + parser run; `--monitor` / `monitor.py` = continuous polling |
| `tools/switch_logs/parse_logs.py` | Errors, warnings (repeats folded), crashes, fps periods, anomalies, shader compiles |
| `tools/switch_tests/run_scenario.py` | Automated input tests (scenarios/*.json), screenshots, termination check, logs |
| `tools/switch_profiling/symbolize.py` | `image+0x..` → function, file:line (aarch64-none-elf-addr2line) |
| `tools/switch_profiling/compare_builds.py` | Build A vs B from two sessions, with a noise-aware verdict |
| `tools/switch_tests/selftest.py` | Tests all of the above against `mock_botbase.py` and `mock_ftpd.py` |

## 1. Files on the Switch SD card

| File (SD root) | From |
|---|---|
| `atmosphere/contents/430000000000000B/exefs.nsp` (124,336 bytes), `flags/boot2.flag`, `toolbox.json` | sys-botbase release ZIP `sys-botBase.zip` (2.6-kill, 14 Oct 2026, SHA-256 `9a2273c4…250733d`) |
| `atmosphere/contents/420000000000000E/exefs.nsp` (169,715 bytes), `flags/boot2.flag`, `toolbox.json` | sys-ftpd-light ZIP `sys-ftpd-light-1.0.5-thorhax1.zip` (31 Oct 2026, SHA-256 `9c1a3747…6efd2035bc37`) |
| `config/sys-ftpd-10k/config.ini` | Written by install_sysmodules.ps1: user `ffxiii2dev`, the generated password, `anonymous:=0`, port 5000 |

- **The folder is `config/sys-ftpd-10k/`, not `config/sys-ftpd/`.** The Thorhax source reads
  `CONFIGPATH "/config/sys-ftpd-10k/config.ini"` (source/util.h), and that is the folder its ZIP ships. The upstream
  README's `config/sys-ftpd/` is wrong for this fork. The fork's own error message also still names the old path.
- Nothing else changes. Atmosphère's configuration is untouched (`atmosphere/config`, `system_settings.ini`, hosts),
  and so are the other sysmodules (`420000000000000B`, `420000000007E51A/B`, ...).
- No ID clashed on 2026-11-05: `430000000000000B` and `420000000000000E` did not exist. If they ever do, the
  installer copies them to `switch/ffxiii2-nx/before_sysmodules/` first and leaves them (pass `-Replace` to overwrite).
- Downloads: the release ZIPs themselves, not the source archives. Their SHA-256 values match the release pages and
  are checked again by the installer.

## 2. Install (card in the PC reader, H:)

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools\switch_remote\install_sysmodules.ps1 -Sd H: ^
  -BotbaseZip ..\_xfer\sys-botBase.zip -FtpdZip ..\_xfer\sys-ftpd-light-1.0.5-thorhax1.zip ^
  -FtpdIni ..\_xfer\sysmods\config.ini
```

Expected output:

```
sys-botBase.zip SHA-256 ok (9A2273C4...)
sys-ftpd-light-1.0.5-thorhax1.zip SHA-256 ok (9C1A3747...)
installed atmosphere\contents\430000000000000B\exefs.nsp (124336 bytes)
...
config.ini written (user set, anonymous:=0, port 5000)
430000000000000B exefs.nsp + boot2.flag: True
420000000000000E exefs.nsp + boot2.flag: True
```

Then do these by hand on the console:
1. Put the card back and boot into CFW (Atmosphère) as usual.
2. sys-botbase is running if the **HOME button LED of a docked Joy-Con lights up** during startup.

If the console does not boot after this, remove `atmosphere/contents/430000000000000B` and `…/420000000000000E`.
Too many sysmodules can exhaust the system memory pool. sys-patch, status-monitor and horizon-oc are already
installed.

## 3. sys-botbase configuration

There is no file. Settings are sent at runtime with `configure <key> <value>` (commands.md). Useful:
- `configure buttonClickSleepTime 80`: hold `click` for 80 ms. The game runs at 8-30 fps on the console, and a
  50 ms click can fall between two frames (commands.md: "make sure this isn't lower than the fps").
- `configure mainLoopSleepTime 10`: faster replies.

The port is TCP 6000 (fixed). The fork's version string is **`2.6-files-kill`** (`getVersion`). switch.py checks it.

Quirks found on the console (handled in botbase.py):
- **Press length:** the client sets `buttonClickSleepTime` from `click_ms` in the config (default 200 ms). A 50 ms
  click on "Press any button" was not seen by the game.
- **`clickSeq`'s `done` arrives late.** The click thread `printf`s it, and stdout is flushed only when the next
  command arrives. So it showed up as the reply to the next `pixelPeek`. `click_seq()` waits for the sequence's
  length, then sends `getVersion` and drops the `done`.
- **Short D-pad names in sequences.** main.c says `DDOWN` and the like do not parse in `clickSeq`; the client
  rewrites `DUP/DDOWN/DLEFT/DRIGHT` to `DU/DD/DL/DR`.
- **Disconnecting cancels a running sequence.** Closing the connection sends `clickCancel` (`release_all`), so
  `click_seq()` returns only once the sequence has run.

## 4. sys-ftpd-light configuration

`sdmc:/config/sys-ftpd-10k/config.ini` (sections `[User] user:=`, `[Password] password:=`, `[Port] port:=5000`,
`[Anonymous] anonymous:=0`, `[Pause]`, `[LED]`, `[Reboot] enabled:=0`). The password is generated, and is kept only
in that file and in `Works\switch_test_config.json`, never in the repository.
- PLUS+MINUS+X pauses or resumes the server.
- The reboot combo stays off.

Known limit (fork README): `poll()` on the console can miss a passive data connection, so about half of the
transfers stall for a few seconds. switchftp.py therefore:
- uses 15 s timeouts;
- retries up to 3 times on a fresh connection, with 2 / 4 / 6 s waits;
- checks every transfer: SIZE after an upload, length after a download.

That is why sys-botbase's file commands are the first choice.

## 5. Network

1. Put the Switch and the PC on the same home network (Wi-Fi or LAN adapter). Neither service is exposed to the
   internet: do not forward ports 5000/6000 on the router.
2. Find the address:
   - on the console: System Settings → Internet → Connection Status → IP address;
   - or run `python tools\switch_remote\switch.py discover --save` from `Works\ffxiii2-k` on the PC. It scans the
     PC's own /24 networks for port 6000 and stores the address if exactly one console answers.
3. Recommended: a DHCP reservation on the router, so the address stays the same.

Central configuration: **`Works\switch_test_config.json`**. It lives outside the repository and is in `.gitignore`.
Template: `tools/switch_remote/switch_test_config.example.json`.

```json
{
  "switch_ip": "192.168.x.y",
  "botbase_port": 6000,
  "ftp_port": 5000,
  "ftp_user": "ffxiii2dev",
  "ftp_password": "<as in config.ini>",
  "nro_dir": "/switch/ffxiii2-nx",
  "log_dir": "/switch/ffxiii2-nx/logs",
  "project": "ffxiii2-nx",
  "launch_sequence": [],
  "launch_wait_s": 90
}
```

Other ways to give the config or the address: `--config <file>`, `FFXIII2_SWITCH_CONFIG`, `FFXIII2_SWITCH_IP`,
`--ip`.

## 6. Verify both services

```
python tools\switch_remote\switch.py status
```

Expected:

```json
{ "switch_ip": "192.168.1.50", "botbase_version": "2.6-files-kill", "botbase_version_ok": true,
  "battery_percent": "87", "application": {"title_id": ""}, "nro_dir": [ {"type": "file", "name": "ffxiii2.nro", ...} ],
  "ftp_root": ["atmosphere", "config", "switch", ...] }
```

- `"botbase": "unreachable: ..."` means sys-botbase is not running, or the address is wrong. Exit code 2.
- `"ftp": "not usable: ... 530 ..."` means the user or password does not match config.ini.

## 7. Connecting from the development session

- Claude writes a job line, for example
  `Push-Location ..\ffxiii2-k; C:\Python314\python.exe tools\switch_remote\switch.py status; Pop-Location`, and
  commits it as `out\job.txt`.
- The runner executes it. With `out\watch.flag` present, it picks up each new `job.txt` within 5 s, without a
  double-click.
- Claude reads `out\job_*.log`.

Interactive use from a terminal in `Works\ffxiii2-k` works the same way.

## 8. Deploy a new NRO

```
python tools\switch_deploy\deploy.py deploy --package ..\builds\<stamp>_ffxiii2_rc5 --name rc5
python tools\switch_deploy\deploy.py deploy --nro app\out\switch\ffxiii2.nro --elf app\out\switch\ffxiii2-nx.elf ^
       --toml dist\switch\ffxiii2-nx\renut_dev.toml --name k160 [--slot experimental] [--restart --collect]
```

1. **Build id and checksums.** The build id is `YYYYmmdd_HHMM_<12 hex of the NRO's SHA-256>`. Every file gets a
   SHA-256. The unstripped ELF is required, and is kept with the NRO, the toml and `manifest.json` in
   `Works\switch_builds\<current|experimental|known_good|failed>\<build id>\`.
2. **Installed version.** Read from `sdmc:/switch/ffxiii2-nx/deploy/current.json`. A console set up by hand has no
   such file. Then its live files are hashed and recorded as `manual_<time>` (known-good).
3. **Running game.** Deploy refuses while an application runs. `--restart` closes it first (`killGame`, then A on the
   error notice).
4. **Backup.** Every live file that will change is *moved* (renamed, not copied) to
   `deploy/<known_good|experimental|failed>/<old build id>/`.
5. **Incremental upload.** Only files whose SHA-256 differs are uploaded, so an unchanged 59 MB
   `deko3d_shaders.bin` is not sent again. `--full` uploads everything.
6. **Verification.** Each upload goes to `<name>.part`. Its size and SHA-256 are read back (`--size-only` skips the
   hash), then it is renamed.
7. **Records.** `deploy/current.json` and `deploy/history.log` on the SD card, `state.json` on the PC.
8. **Never touched:** `renut/` (saves), `logs/`, `before_rc*/`, `game_root/`, `update/`, `dlc/`, `shader_capture/`.

Then:
- `deploy.py mark-good` after a successful test. The build becomes the last known-good one, and is backed up into
  `known_good/` when it is replaced.
- `deploy.py mark-failed` records a failed build (it moves to `failed/` when replaced).
- `deploy.py rollback [--build <id>]` puts the newest known-good build back.
- `deploy.py status` shows both sides.

## 9. Start and restart the game

- **Stop:** `switch.py kill` (sys-botbase `killGame`). The console shows "The software was closed because an error
  occurred". `click A` returns to HOME.
- **Start:** `python tools\switch_remote\home_launch.py [--kill]`. This console has a "Final Fantasy XIII-2"
  forwarder tile on HOME (full application memory). HOME orders the tiles by last use and the row wraps around. A
  fixed button sequence therefore started Zelda Dusklight on 2026-11-05 (closed again at its title menu).

  home_launch.py:
  1. Goes to the tile row (HOME, DD, DU).
  2. Takes a screenshot and finds the cyan selection frame.
  3. Compares the selected tile with `tools/switch_remote/ref_tile_ffxiii2.png` (48x48 grey). The difference is
     0-1.1 for FF XIII-2 and 58-131 for other tiles; the match limit is 20.
  4. Steps DLEFT until the tile matches (at most 40 steps), presses A, and waits for a new `renut_NNN.log`.

  deploy.py `--restart` and the scenarios' `launch` step use it (`launch_method` "tile", the default;
  "sequence" uses `launch_sequence`). It needs PIL; the PC's Python 3.14 has 12.3.0.
- **Proof that the new build runs:** a new `logs/renut_NNN.log` appears. One file per boot, numbered upward, with
  `[deko3d] DKSH cache ...: N variants` in its first lines.

## 10. Logs: where and how

Investigated in the source on 2026-11-05.

| File (next to the NRO) | What | Written |
|---|---|---|
| `logs/renut_NNN.log` | spdlog of the run: info / warning / error, `[renut] T s: N game frames (F fps), render thread busy R ms, GPU G ms` every 10 s, `[audiodiag]`, `[xmak]`, deko3d compile lines | A writer thread (sdk/src/core/logging.cpp `WriterFileSink`) batches 64 KB and does `fwrite` + `fflush` at least once a second. Only when `log_file_writer = true`, which renut.toml / renut_dev.toml set; the release defaults are off |
| `logs/ffxiii2_NNN.log` | the game's own output | same |
| `logs/rex/rex_profile.log` | switch_perf.cpp: a 10 s report (game fps, CPU per thread, libnx calls, clocks, audio), stack samples when sampling is on | rewritten at start, then opened ("a") and closed for each report |
| `logs/rex/rex_crash.log` | abort() / exception: the stack as `image+0x..`; error 2345-0101 | at the crash |
| `logs/rex/rex_stderr.log` | stderr, unbuffered | continuously |
| `deko3d.log` (NRO folder) | the deko3d cache log | with log_file_writer |

- **Crash information:** `rex_crash.log`, and `[error]` / `[critical]` lines in `renut_NNN.log`.
- **Performance information:** the `[renut]` periods, and `rex_profile.log`.
- **Reading while the game runs:** the files are flushed (see the table), so a read sees them up to the last second.
- **Verified on hardware (2026-11-05): a file the game holds open cannot be read.** Horizon's FS refuses the second
  open: `fileList` reports size -1 and `fileRead` -1 (renut_NNN.log, rex_stderr.log, deko3d.log).
  - collect.py records these as `locked` in `session.json` and fetches them on the next collection, after the game
    has closed them.
  - The game's handles are never touched.
- **Non-blocking logging:** `log_file_reopen = true` (sdk/src/core/logging.cpp, from k158; set in renut_dev.toml).
  The log writer closes the file after each batch (at most one a second) and reopens it with "ab" for the next, so
  the file is free between batches. The monitor then sees the log within about a second; a read that hits a batch
  is retried on the next poll.

Collect:

```
python tools\switch_logs\collect.py --session after_battle
```

This writes to `logs\switch\<build id>\<session>\`:
- the files, keeping their SD-relative paths;
- `session.json`: remote path, size, SHA-256, status (`ok` / `growing` / `locked` / `incomplete`), time, transport,
  build id;
- `report.txt` / `report.json`.

Only new or changed files are fetched (`logs\switch\index.json`; `--all` fetches everything). An existing session
name gets `_2`, `_3`, ...: nothing is overwritten. A file whose size changed during the read is read once more, then
marked `growing`.

`report.txt` sections: `!! CRASH / FATAL`, `!! ERRORS` (folded by message), `!  WARNINGS` (repeats first),
`PERFORMANCE` (fps per 10 s period), `!  PERFORMANCE ANOMALIES` (periods under 5 fps after 30 s, render thread
over 45 ms a frame), `rex_profile.log`, `SHADERS` (run-time compiles).

### Controller input for tests

The console in handheld mode makes the Joy-Cons player 1, and sys-botbase's virtual Pro Controller player 2
(renut_014: "Pro Controller (No1) is player 2"). The game reads player 1 only.
- `input_shared_players = true` (sdk/src/input/input_system.cpp, from k158; set in renut_dev.toml) lets every
  controller drive player 1 (SharedAssignment, states merged).
- The ImGui overlay reads all controllers itself (padInitializeAny), so L3+R3 works without it.

## 11. Continuous monitoring

```
python tools\switch_logs\monitor.py --every 5 --minutes 30 --session bahamut
```

Every 5 s, the monitor fetches what grew and prints only what is new: new error / warning kinds (with counts),
CRASH, the latest fps periods, anomalies. Each poll is stored as a session of its own (`bahamut_0001`, ...),
under the build id.

## 12. ImGui overlay (developer builds)

The overlay is the existing profiler window (`app/src/profiler/profiler_overlay.h`, ported from UnleashedRecomp).
It is drawn by the same ImGui drawer as Renut's overlays, on deko3d. No renderer change was made for it.
- **Toggle:** hold **L3 + R3** (both stick clicks). It works only when `banjo_profiler_pad_toggle = true`, as in
  `dist/switch/ffxiii2-nx/renut_dev.toml`. The release defaults keep it false (`app/ffxiii2_switch_defaults.toml`).
- **Other combo:** `banjo_profiler_pad_combo = "ZL+ZR+MINUS"`, for example. The game still sees the buttons;
  FF XIII-2 uses L3/R3 only singly.
- **Shown, measured:**
  - frame-time graph (256 UI frames);
  - Application (frame time and fps), Update, Draw, frame limiter, GPU frame, host present, render thread (ms and
    average);
  - CPU % per thread (once a second);
  - stutter: frames over 45 / 100 ms;
  - process memory used / total (svcGetInfo);
  - deko3d draw calls per second and per frame, shader compiles (count, rate, average ms, failures), DKSH cache hits
    and additions, GPU image / buffer MB, texture uploads per second.
- **Shown as "not measured":** allocation statistics and asset loading. No counter exists for them; they are not
  estimated.
- **Logging:** the 10 s `[renut]` lines and `rex_profile.log` already carry these numbers. The writer thread is the
  log writer, at guest priority.
- **Ships with:** the first Switch build after 2026-11-05 15:30 UTC (sources changed: profiler_overlay.h,
  profiler.cpp/.h, banjo_renut_deko3d.cpp).

## 13. perfil_pilas.flag (stack sampling profiler)

This project already had the sampler that NFSMW-NX uses: the same `sdk/src/ui/switch_perf.cpp`. It samples every
thread at 1 kHz with `svcGetThreadContext3`, along the x29 chain. It writes a report every 10 s, with a
"during the hitches" section for frames over 45 ms. So it was **adapted, not replaced**. It now accepts both flag
names in `<NRO folder>/logs/rex/`:
- `perfil_pilas.flag` (NFSMW-NX's name; from the next build);
- `stacks_profile.flag` (works with rc4 already).

`switch_profile_stacks = true` in the toml also turns it on.

```
python tools\switch_remote\switch.py put empty.flag /switch/ffxiii2-nx/logs/rex/perfil_pilas.flag
```

(`empty.flag` is an empty file on the PC; `fileWrite` with 0 bytes also works.)

- Restart the game. The flag is read once, 8 s after start. The report says `Stack sampling: active (1 ms)`.
- Remove the flag (`switch.py rm ...`) and restart to turn it off.
- **The sampler costs frame rate: its fps are never benchmarks.** Use its percentages to find where the CPU goes.

## 14. Symbolize profiler output

```
python tools\switch_logs\collect.py --session prof1
python tools\switch_profiling\symbolize.py logs\switch\<build id>\prof1\logs\rex\rex_profile.log
```

- The ELF is found from the session's build id in `Works\switch_builds\*\<build id>\`, or given with `--elf`. It
  must be that exact build.
- symbolize.py runs `C:\devkitPro\devkitA64\bin\aarch64-none-elf-addr2line.exe -f -C -e <ELF>`.
- Output: `rex_profile_named.txt`. Every `image+0x..` is followed by `[function file:line]`. A summary per thread
  lists:
  - `pc`: where the CPU was, by sample share;
  - `lr`: called from;
  - `stack`: function <- caller;
  - `wait`: kernel waits;
  - a separate block for the frames over 45 ms.
- The same tool names `rex_crash.log`.

## 15. Automated tests

```
python tools\switch_tests\run_scenario.py --list
python tools\switch_tests\run_scenario.py idle_perf_60s.json --session base_rc4
```

Scenarios (`tools/switch_tests/scenarios/`) are taken only from what is known about this game:
- **boot_to_title:** launch, wait for the new renut log, screenshots every 5 s, title screen.
- **title_continue:** A at the title, then A on the first item. CONTINUE loads the newest save (console rc4, user
  confirmed; Ryujinx r19). 60 s in the field. Never opens a menu that writes a save.
- **idle_perf_60s:** no input for 70 s. The A/B baseline: run it at the same spot for both builds.
- **walk_field:** stick forward / back, a camera turn. For streaming hitches and the flicker bands.
- **pc_sequence_opening_battle:** the PC / Ryujinx input sequence (title → New Game → opening → Chaos Bahamut
  battle). Its timings are the PC's, so check them against the screenshots on the console. New Game writes no save by
  itself.

Each run writes `logs\switch\<build id>\<session>\`:
- `shots\NN_<name>.jpg`;
- `result.json`: steps with times, duration, passed / failed / interrupted, whether the app still runs, a screenshot
  at a failure;
- the logs.

Every pressed button is released and both sticks are centred at the end, also after an error or Ctrl+C.
`check_running` fails the test when the application is gone (a crash).

A/B (the optimisation loop):

```
python tools\switch_profiling\compare_builds.py logs\switch\<A>\base_rc4 logs\switch\<B>\base_k160
```

The verdict counts only when:
- both sides ran the same scenario;
- the stack sampler was off on both;
- the mean fps difference exceeds both `--min-gain` (3%) and the spread of the 10 s periods.

## 16. Troubleshooting

| Symptom | Check |
|---|---|
| `discover` finds nothing | Joy-Con LED at boot? Same network (Wi-Fi isolation / guest network blocks it)? Windows firewall: outgoing connections are allowed by default; no rule change is made by these tools |
| `getVersion` gives something other than `2.6-files-kill` | Another sys-botbase build (olliz0r's) is installed: no file commands. Reinstall the fork's ZIP |
| `err sd` | sys-botbase could not mount the SD card (rare; reboot) |
| Paths with spaces | sys-botbase file commands cannot take them (`Test 1/` folders): use FTP (`--transport ftp`) |
| FTP 530 | user / password differ between config.ini and switch_test_config.json |
| FTP transfer stalls | The known poll() issue: switchftp retries 3 times; prefer `--transport botbase` |
| `.part` file left on the SD card | An interrupted upload. The next put deletes it, the real file is untouched |
| Deploy refuses (application running) | Close the game, or `--restart` |
| No new renut log after launch | `log_file_writer = true` in ffxiii2.toml? `launch_sequence` right? Check a `switch.py shot` |
| Overlay does not open | NRO older than the overlay change, or `banjo_profiler_pad_toggle` false in ffxiii2.toml |
| symbolize: no ELF | Deploy keeps the ELF; for older builds give `--elf builds\<pkg>\ffxiii2-nx.elf` |

## 17. What still needs hands on the console

1. Put the SD card back after the install, and boot into CFW. Confirm the Joy-Con HOME LED.
2. Same network as the PC. Note the IP (or let `discover --save` find it).
3. Record `launch_sequence` once (9), or start the game by hand when a tool asks.
4. While a scripted test runs, do not touch the controller. sys-botbase's virtual controller is player 1.
