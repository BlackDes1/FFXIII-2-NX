# ffxiii2-nx

Final Fantasy XIII-2 (Xbox 360, title 53510806, **Europe disc + TU6**) ported to Windows and then Nintendo Switch by
static recompilation with ReXGlue (PPC -> C++), the method of sa2-nx / nfsmw-nx. Uses the sa2-nx SDK fork.

No game data, no generated code in this repository: your extracted game is in `..\game_data` / `..\game_root`, the
generated C++ in `app\generated` (git-ignored).

## Status
- Stage 1, extract + inspect: done (`docs/stage1-inspection.md`).
- Stage 2, codegen + missed-code sweeps: done in the cloud workspace (`docs/stage2-codegen.md`); to be reproduced on
  Windows by the scripts below.
- Stage 3a, headless CPU-side boot (cloud, no GPU): done. Builds, links, runs xstart to the Direct3D wait for the
  GPU with no crash (`docs/stage3-boot.md`). Two fixes found on the way (import entries, game paths from the toml).
- Stage 3b, boot on PC with the emulated GPU (Windows, D3D12): New Game -> opening -> first battle playable in
  scripted tests. Fixed: freeze after the first button press (graphics interrupt handler), ~100 s frozen loading
  screen (memexport readback), dropped profile settings (SDK patch). `docs/stage3-boot.md`.
- Play by hand: `play.cmd` (sound, window, keyboard). Scripted test: `tools\win\test_newgame.cmd`.
- Crash-class audit of the generated code (pointers into code, jump tables, setjmp/longjmp, extents): no entry
  missing or wrong (`docs/crash-class-audit.md`).
- Stage S1, Switch build: the NRO builds (devkitA64 + static NVK, the four zone modules linked in) and starts in Eden
  up to Vulkan device creation, where Eden itself crashes in every configuration tried. Not yet run past that: needs
  the console or another emulator. `docs/switch.md` (also the plan from the guide repositories).

## Windows (PowerShell, from this folder)
Needs Git, Python 3, VS 2022 Build Tools (C++ + Windows SDK) and a portable LLVM clang 20 in `..\_tools\llvm`.
`tools\win\install_prereqs.ps1` installs all four (winget + the official LLVM installer; asks for admin once) and
then runs setup.ps1.
```
powershell -ExecutionPolicy Bypass -File tools\win\install_prereqs.ps1   # prerequisites + setup.ps1
powershell -ExecutionPolicy Bypass -File tools\win\setup.ps1     # sa2-nx SDK, third-party sources, game_root, rexglue.exe
powershell -ExecutionPolicy Bypass -File tools\win\codegen.ps1   # default.xex + 4 zone DLLs -> app\generated
powershell -ExecutionPolicy Bypass -File tools\win\build.ps1     # out\pc\ffxiii2.exe + zone DLLs
powershell -ExecutionPolicy Bypass -File tools\win\boot_test.ps1 # 60 s, muted + minimized, end of the log
```

## Switch (PowerShell, from this folder)
Needs devkitPro (devkitA64 + libnx) in `C:\devkitPro` and the Mesa Horizon SDK zip unpacked to `..\_tools\mesa-switch`.
```
powershell -ExecutionPolicy Bypass -File tools\win\build_switch.ps1            # out\nx\ffxiii2.nro
powershell -ExecutionPolicy Bypass -File tools\win\package_switch.ps1 -CopyGame # SD folder out\nx\sdcard\switch\ffxiii2-nx
powershell -ExecutionPolicy Bypass -File tools\win\eden_run.ps1 -Name run1     # Eden test run (docs/switch.md)
```
`run_job.cmd` runs the command lines of `out\job.txt` in a minimized window (used for test runs).

## Analysis tools (`tools/local`, written for this port)
xdvdfs.py (ISO), stfs.py (TU package), xexinfo.py, dis.sh (llvm-objdump on an image dump), extent.py / gapcheck.py
(unresolved branch targets), ptrscan.py (gaps, pointer-only functions, thunks), sjlj.py (setjmp/longjmp by shape),
post_codegen_modules.py (zone module lists after per-module codegen). `tools/local/audit`: the crash-class audit's
scanners (midscan, classify, falloff, sjlj2, unemitted, ...).
