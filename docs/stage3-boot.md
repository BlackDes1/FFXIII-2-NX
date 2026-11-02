# Stage 3: boot (2026-11-02)

## 3a. Headless CPU-side boot in the cloud workspace (no GPU) - done

Build: the whole project for Linux (clang 18, `-march=x86-64-v2`, generated code at -O3) against the sa2-nx SDK
fork, with the cloud-only shims of docs/stage2-codegen.md (now `-include concepts_fix.h`, which raises
`__cpp_concepts` only for C++20+ units; a plain `-D` broke glslang, which is C++17) and SDL's dummy video/audio
drivers. Result: `ffxiii2` (107 MB) + `libffxiii2_z0070/z0130/z0160/z0161.so`, no compile errors.

Run: `tools/linux/boot_test.sh` with `gpu_plugin = ""` (GPU emulation off), game root with the real xex/xexp, the
small archive index files and sparse placeholders for the large archives, TU6 data as `update:`.

Found and fixed on the way:
1. **Link error, 11 undefined `sub_82C30EB0` ... `sub_82C311C0`**: stage-2 pass-2 entries in the import-thunk
   table (xboxkrnl NtOpenFile, NtCreateFile, NtClose, NtReadFile, NtReadFileScatter, NtQueryInformationFile,
   NtQueryVolumeInformationFile, NtQueryFullAttributesFile, NtQueryDirectoryFile, NtSetInformationFile,
   NtDuplicateObject by ordinal). The game keeps their addresses in a .data table (0x82CCF900), which is why the
   first pointer pass took them for functions. Removed; `__imp__NtOpenFile` etc. are bound again. This would have
   broken the Windows link too.
2. **"--game_data_root was not provided"**: the SDK resolves the game/update roots before it reads ffxiii2.toml.
   `Ffxiii2App::OnConfigurePaths` now reads `game_data_root` / `update_data_root` from the toml when the command
   line does not give them (tools\win\build.ps1 writes them there).

What the boot does (log + gdb thread dump after 20 s):
- mounts game: and update:, applies TU6 (0.0.0.11 -> 0.0.6.11), starts `xstart`;
- looks for `game:\white-extension.xex` and `game:\bin\white-extension.xex` (not on the disc) and opens
  `DEVKIT:\alba_data\bootpath` (no DEVKIT device on a retail console either): both fail as on retail;
- creates its threads (two "CDevCommon Task" workers wait in NtWaitForSingleObjectEx, normal);
- main thread: xstart -> sub_829142D8 -> ... -> sub_82852A20 -> sub_82851390 -> **sub_82856928**, the Direct3D
  wait for the GPU: spins (`mr r31,r31` priority hints) polling the ring-buffer read pointer written back by the
  GPU (device+11024). With GPU emulation off it never advances. **No crash, no unregistered call, no illegal
  instruction in 60 s.** No game file is read before this point (read trace: 0 events).

## 3b. Boot on Windows with the emulated GPU (D3D12) - title screen reached

PC: AMD Radeon 680M-class iGPU (DXGI device 0x1681), Windows, clang 20.1.8, VS 2022 Build Tools. Run with
`run_stage3.cmd` / `run_boot.cmd` (codegen -> build.ps1 -> boot_test.ps1, always muted + minimized). Codegen on Windows
gives the same function table as the cloud run (ffxiii2_register.cpp identical); the first full build took ~6 min.

Tools added for this stage:
- `ffxiii2_shots` / `boot_test.ps1 -Shots "10,20,..."`: the presented guest frame saved as BMP at those seconds
  (src/ffxiii2_capture.cpp, SDK presenter CaptureGuestOutput), so a minimized run can be checked visually.
- `boot_test.ps1 -Pad "12:A,24:START,..."`: scripted pad input (the SDK's `input_script`), no controller needed.
- `REX_CACHE_ROOT` per run: an empty shader cache, so runs are comparable (a warm cache hides timing problems).

Boot: TU6 applied, Direct3D 12 device up, `SetInterruptCallback(828510C8, 40002180)` (same handler address as the
USA build), audio running, "Press any button to continue." drawn correctly. With input: opening movie (credits at
~22 s) -> **Final Fantasy XIII-2 title screen** (~40 s) -> "Begin game in NORMAL MODE?" dialog. Logged once per
boot: `XamUserWriteProfileSettings: Unimplemented data type 1` (SDK; no visible effect so far).

### Freeze after the first button press: graphics interrupt handler under the global mutex

Symptom (first input run): black screen from the first press on, one core busy, no further frames.
ReXGlue's `FunctionDispatcher::ExecuteInterrupt` holds the runtime's global mutex for the whole graphics interrupt
handler sub_828510C8; the handler takes kernel spinlocks, and a game thread holding one of them that needs the
global mutex waits forever while the handler spins holding the mutex. (Same freeze documented for the USA build by
leftisthominid/ffxiii-2-port; the hook here, src/ffxiii2_irq.cpp, is written independently.)

Evidence (each run: empty shader cache, same scripted input, interrupt log every 5 s):

| mode | runs | frozen | log of the frozen runs |
|---|---|---|---|
| ReXGlue's lock (`FFXIII2_IRQ_LOCK=global`) | B1, B2, G1-G4 | 2 of 6 (G2, G4) | `INSIDE the handler for 60823 ms`, 0 interrupts afterwards, CPU busy |
| handler outside the global mutex (default) | A, A1, U1-U4 | 0 of 6 | (all reach the title; longest handler call < 0.4 ms) |

(Plus the first input run, without any hook: frozen.) 0 of 6 is good but not conclusive evidence: with a 1-in-3
freeze rate, six clean runs by chance have ~9% probability; more runs come with every later test.

Fix (default): the handler runs with the mutex level ExecuteInterrupt took released, as interrupts on the console
are masked only on the hardware thread that disabled them.

### New Game -> opening -> first battle; ~100 s frozen loading screen (memexport readback)

Scripted New Game runs (title -> "Begin game in NORMAL MODE?" Yes -> new save on "Xbox 360 Hard Drive" -> opening
cutscenes with Lightning and Caius -> Bahamut Chaos battle with the "Learn about the battle basics?" prompt): no crash
in 12 min, steady 30 fps in the cutscenes, 60 fps in menus. The save is created ("[save] create 'ff13-00.dat'").

Problem: the transition from the opening into the battle presented no frame for ~100 s (game frames 0/s from ~430 s
to ~530 s; no file I/O in that window, music kept playing).
CPU profile of 440..500 s (src/ffxiii2_profile.cpp, `boot_test.ps1 -ProfileAt 440 -ProfileFor 60`, PDB names):
- Main thread 100 % busy: 55 % sub_82856928 + 30 % sub_82850D88 = the game's D3D wait for the GPU (ring buffer);
- GPU command thread: 54 % `CheckSubmissionFence` under `IssueDraw_MemexportReadbackFullPath`, 43 % copying.
Every draw whose shader writes to memory (memexport) made the emulated GPU wait for the host GPU and copy the result
back to guest memory; the SDK's "fast" readback falls back to the full synchronous path inside one submission.

Fix: `readback_memexport = false` as the app's built-in default (Ffxiii2App::BuiltInConfig; ffxiii2.toml can still
turn it on). Same run with readback off: the transition takes ~15 s at 30 fps (one 5-s window at 23 fps), CPU over
the run 319 s instead of 509 s, and the opening (300 s, 400 s) and battle frames look the same as with readback on.
Upstream Xenia also defaults to no memexport readback. Not verified: later scenes that might read memexported data on
the CPU (would show as wrong animation or effects).

Next: past the title (New Game -> opening; the USA project also reports a loading-screen freeze after the opening
battles, which this fix should cover), then save handling (`XamUserWriteProfileSettings` data type 1), then stage 4.
