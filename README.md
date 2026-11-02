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
- Stage 3b, boot on PC with the emulated GPU: the game reaches the opening movie and the title screen (Windows,
  D3D12). Freeze after the first button press found and fixed (graphics interrupt handler, `docs/stage3-boot.md`).

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

## Analysis tools (`tools/local`, written for this port)
xdvdfs.py (ISO), stfs.py (TU package), xexinfo.py, dis.sh (llvm-objdump on an image dump), extent.py / gapcheck.py
(unresolved branch targets), ptrscan.py (gaps, pointer-only functions, thunks), sjlj.py (setjmp/longjmp by shape),
post_codegen_modules.py (zone module lists after per-module codegen).
