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

## 3b. Boot with the emulated GPU (Windows, D3D12) - next

Needs the PC: `tools\win\setup.ps1`, `codegen.ps1`, `build.ps1`, `boot_test.ps1` (README). The cloud workspace has
no Vulkan driver (no GPU, no software rasteriser installable), so the Xenos emulation cannot run there.
Expected next findings (USA project, same code layout): the graphics interrupt handler (USA sub_828510C8) holding
the runtime's global mutex -> loading-screen freeze after the first battles; zone module load paths.
