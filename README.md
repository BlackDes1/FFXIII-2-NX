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
- Stage 3b, boot on PC with the emulated GPU: next. Needs the Windows build.

## Windows (PowerShell, from this folder)
Needs Git, Python 3, VS 2022 Build Tools (C++ + Windows SDK) and a portable LLVM clang 20 in `..\_tools\llvm`.
```
powershell -ExecutionPolicy Bypass -File tools\win\setup.ps1     # sa2-nx SDK, third-party sources, game_root, rexglue.exe
powershell -ExecutionPolicy Bypass -File tools\win\codegen.ps1   # default.xex + 4 zone DLLs -> app\generated
powershell -ExecutionPolicy Bypass -File tools\win\build.ps1     # out\pc\ffxiii2.exe + zone DLLs
powershell -ExecutionPolicy Bypass -File tools\win\boot_test.ps1 # 60 s, muted + minimized, end of the log
```

## Analysis tools (`tools/local`, written for this port)
xdvdfs.py (ISO), stfs.py (TU package), xexinfo.py, dis.sh (llvm-objdump on an image dump), extent.py / gapcheck.py
(unresolved branch targets), ptrscan.py (gaps, pointer-only functions, thunks), sjlj.py (setjmp/longjmp by shape),
post_codegen_modules.py (zone module lists after per-module codegen).
