# Stage S1: the Switch build (2026-11-03)

The first Nintendo Switch build: the same generated code as the PC build, compiled with devkitA64 and linked into one
NRO with the four zone modules, the SDK's Horizon layer and the emulated Xenos GPU on static NVK (Mesa). Then the
first runs in the Eden emulator, with their evidence. The native renderer (stage 4) and the GPU work (stage 5) come
after; the plan, from the guide repositories, is at the end.

## Result

| Step | State | Evidence |
|---|---|---|
| Build (devkitA64 GCC 16.1.0, libnx, Mesa 26.2.4 Horizon SDK) | **done** | `out\nx\ffxiii2.nro`, 72 MB; ELF archived in `out\nx\elf\` |
| `_start` at 0, the driver version, the four zone modules | **checked** | `nm`: `_start` = 0; `strings`: `Mesa 26.2.4 (git-606441c90e)`; `ReXModule_Register_ffxiii2_z0070/z0130/z0160/z0161` present |
| NRO starts in Eden v0.2.1, reads its toml, opens the SD log, creates the Vulkan instance, finds `NVIDIA Tegra X1 (GM20B) (NVK GM20B)`, creates the device | **seen** | `logs\ffxiii2_001.log` of runs 6-12 (below) |
| Past Vulkan device creation in Eden | **no**: Eden itself crashes 5-8 s after start | exit code 0xC0000005 of eden.exe, 7 runs |
| Game boot on the Switch (title, menus, gameplay) | **not verified** | needs an emulator that runs NVK, or the console |

## Building

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\build_switch.ps1     # out\nx\ffxiii2.nro
powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\package_switch.ps1 -LinkGame   # SD folder for an emulator
powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\package_switch.ps1 -CopyGame   # SD folder for a console
```

- Needs devkitPro (devkitA64 + libnx, `C:\devkitPro`), the Mesa Horizon SDK zip unpacked to `Works\_tools\mesa-switch`,
  and the VS 2022 Build Tools' CMake + Ninja (through sa2-nx's `env.ps1`). The generated code is the PC's
  (`tools\win\codegen.ps1`): a cross build cannot run rexglue.
- The full build compiled 771 objects with 7 jobs (16 cores, 15 GB RAM: one job per 2 GB) in about 10 minutes (configured 06:50,
  first link about 07:00 local time); a rebuild after one SDK file changed took under a minute. No LTO and no PGO yet.
- `app/CMakeLists.txt`, Switch part (method of sa2-nx):
  - `-mearly-ra=none` (GCC 16.1's AArch64 early register allocation dropped table lookups in Peggle 2's console build);
  - each `*_register.cpp` at `-O0` (one huge function run once at start);
  - each zone module partially linked (`ld -r --force-group-allocation`), its hidden symbols made local and its two entry
    points renamed (`objcopy --localize-hidden --redefine-sym`), registered by `src/ffxiii2_static_modules.cpp` under the
    names of `generated/default/module_registry.cpp` (`ffxiii2_z0070` ...): an NRO cannot load libraries, and all four
    modules share base 0x88800000 and their generated symbol names;
  - `ffxiii2_switch_defaults.toml` compiled in (BuiltInConfig), `ffxiii2_switch.toml` written next to the NRO when missing;
  - `REX_SWITCH_FALLBACK_EXE=/switch/ffxiii2-nx/ffxiii2.nro` (an emulator gives no argv[0]).
- Game files on the SD card: `sdmc:/switch/ffxiii2-nx/game_root` (disc + TU6 `.xexp` beside each `.xex`) and
  `.../update` (TU6 data files), found by `OnConfigurePaths` next to the executable. `asset_pack_build = false`: the disc
  is ~8 GB, so no pack is built at start (a pack is still used if one exists).

Three toolchain problems, each with its fix in the scripts:

1. **`libnx linker specs not found`** at the first configure: devkitPro's installer exports `DEVKITPRO=/opt/devkitpro`
   (an MSYS path) and the toolchain file prefers the environment over `-DDEVKITPRO`, also in CMake's compiler checks.
   `build_switch.ps1` sets `$env:DEVKITPRO` to the Windows path.
2. **The configure stopped at the first CMake deprecation warning**: Windows PowerShell 5.1 turns a native command's
   stderr line into a terminating error under `$ErrorActionPreference = 'Stop'` (nfsmw-nx `docs/toolchain.md` names the
   same trap). The script runs native tools with `Continue` and checks exit codes.
3. **Undefined `core::panicking::*` from `libvulkan.a(nil...)`** at the link: the Mesa 26.2.4 SDK's `libvulkan.a` contains
   the Rust core library (NAK and nil are Rust) but its archive index did not list those members (13,399 index entries;
   `panic_fmt` defined in member `core-e288075ad1e23491...cgu.0.rcgu.o` but absent from the index). Re-indexing it with
   ranlib gives 18,601 entries and the link succeeds. `build_switch.ps1` does this once with devkitA64's ranlib and keeps
   the original as `libvulkan.a.orig`.

## Testing in Eden

`tools\win\eden_run.ps1 -Name <run> [-Seconds N] [-Package] [-Frontend qt|cli]` runs one test and collects Eden's log,
the NRO's SD logs (`logs\`, including `logs\rex\rex_crash.log`) and captures into `out\eden_runs\<run>`.
`run_job.cmd` + `out\job.txt` start any such command line minimized (one job file per run, renamed when it starts).

The setup, and why:

- **A private, portable Eden.** `Works\eden_test\eden.exe` is a copy of `Port\eden.exe` with a `user\` folder beside it
  (keys, `config\qt-config.ini`). Eden looks for `user\` next to the executable: in the working folder it was ignored
  (run 5 read and rewrote `%APPDATA%\eden\config\qt-config.ini`, which it does on every boot; no value was changed by
  this port). The portable config is the user's, with `sdmc_directory` = `out\nx\sdcard`, audio muted, volume 0,
  handheld mode, `flush_line` on, and the debugging options of the user's config **off**: with `use_gdbstub` on, run 5's
  boot stopped at "Starting server on port 6543" waiting for a debugger. The NAND (firmware) path stays the user's.
- **eden-cli is not usable** in this Eden build: it exited at start with code 3 (abort) and an empty log in runs 2-4,
  with and without changes to its config.
- **Minimized and muted**: `eden_run.ps1` minimizes the emulator's window while it starts and stops only its own process
  (by id).

### Runs (Eden v0.2.1, AMD Radeon iGPU, Vulkan unless noted)

| Run | Change | Result |
|---|---|---|
| 6 | first portable run | NRO log: Vulkan instance, device `NVK GM20B`, its features; Eden log: 41 `HW.Memory <Error> Unmapped Write64 @ 0x00000035141FA000 ...` (a zeroing loop, plus one `Write32 @ +0x50000001C = 0x383F6E2A`); eden.exe crashed at 8 s (0xC0000005) |
| 7 | `switch_guest_views_eager = true` (new SDK key, below) | same; 36 unmapped writes; crash at 6 s |
| 8 | + `NVK_SWITCH_CPU_WRITE_MEM_UNCACHED=0` (new key `ffxiii2_mesa_env`) | same; crash at 5 s |
| 9 | + Eden log `Kernel.SVC:Debug Service.NVDRV:Debug` | no unmapped writes this time; the last calls are NvMap creates/allocs, GPU address-space maps and `svcSetMemoryAttribute(uncached)`; crash at 7 s |
| 10 | + `NVK_SWITCH_MAPPED_COMPLETION=0`, `MESA_SHADER_CACHE_DISABLE=1` | crash at 6 s, during the NvMap/AllocSpace ioctls that follow `vkCreateDevice` |
| 11 | Eden's OpenGL backend | the same crash at 7 s |
| 12 | Vulkan, Eden's macro JIT and macro HLE off | the same crash at 6 s |

What this shows: the NRO's own start-up works in the emulator (Horizon services, the SD card, the toml, the log, the
static Vulkan driver), and every run ends in **eden.exe itself** during the ioctls of NVK's device creation (the last SDK
log line is the device feature list, written right before `vkCreateDevice` returns), whatever Eden's renderer, macro
and memory settings. The unmapped writes of runs 6-8 did not reappear after run 8, and the crash did not change when
they went away, so they are not its cause. NVK on Horizon has only been verified on the console (nfsmw-nx, sa2-nx up to
1.0.2.1); the ports that test in an emulator (sa2-nx, banjo-nx) do it with deko3d in Ryujinx.

Not verified: where in Eden the crash is (no symbols for eden.exe, no crash dump), and whether this NRO boots on a
console or in Ryujinx.

### SDK and app changes made for this

- `tools/sdk_patches/0002-switch-guest-views-eager-cvar.patch`: SDK key `switch_guest_views_eager` (default off). The
  SDK maps guest memory views lazily from the data-abort handler on a console, and eagerly at commit when there is no
  loader (Ryujinx). Eden passes a process handle, so the SDK chose the lazy path although Eden delivers no data aborts
  (its log showed the writes dropped). The key takes the eager path on request; leave it off on a console (process
  limit on mappable memory, nfsmw-nx `docs/platform-notes.md`).
- `ffxiii2_mesa_env` (`app/src/ffxiii2_switch_cvars.cpp`): Mesa/NVK environment variables set before the Vulkan
  instance (`"NAME=value;..."`); the names the linked driver reads were taken from `libvulkan.a`'s strings.
- `switch_profile_report`, `switch_profile_stacks`: the keys the SDK's Switch profiler reads by name (sa2-nx's).

## What the guide repositories say, applied to this port

Read for this stage: sa2-nx (`docs/porting-guide.md`, `building.md`, `switch-testing.md`, `deko3d-renderer-guide.md`,
`renut-renderer.md`), nfsmw-nx (`docs/platform-notes.md`, `toolchain.md`, `measuring.md`, `console-overlays.md`,
`extras/saltynx-template`, and on branch `deko3d-implementation` `docs/HowToDeko3d.md` and `docs/deko3d-devlog.md`),
renut-nx (`docs/porting-start-here.md` and its reading list), bejeweled3-nx (`docs/optimising-functions.md`),
SwitchWakerHD (`docs/debug-server.md`), mono-nx and switch-gdx (other runtimes on libnx; nothing specific to take).

Platform facts that decide the design (nfsmw-nx `platform-notes.md`):

- Three cores at 1020 MHz; GPU 768 MHz docked, 307.2 MHz handheld unless the title asks for 460.8 MHz (config
  0x92220008). Only priority 0x3B is time-sliced: a busy thread at any other priority starves its core.
- Memory: about 3.2 GB of heap under hbloader (title takeover or a 39-bit forwarder; applet mode gets ~400 MB). Commit
  guest memory in chunks and map views on first touch (the SDK does this); the exception path needs a pool of stacks
  (the SDK has it).
- NVK: every fence check and submit is an IPC; GPU allocations are slow (~1.9 ms per dedicated texture) and limited in
  number (NvMap handles; raise `__nx_nv_transfermem_size` if `0x235C` appears); pipelines ~59 ms each without a cache.
- Presentation: one queue, three swapchain images, FIFO or IMMEDIATE only.

The road the earlier ports took, which this port follows:

1. **Native renderer instead of the emulated GPU** (stage 4). sa2-nx went emulated GPU on NVK -> native Vulkan renderer
   (masseffect-nx's) -> Renut's Direct3D-level renderer on deko3d (1.0.3), and dropped NVK (NRO 58 -> 47 MB). deko3d
   removes pipeline creation, NVK's per-draw CPU and the Mesa build; its shaders go HLSL -> DXC -> SPIR-V -> SPIRV-Cross
   -> GLSL -> uam -> DKSH, made from the player's own game files. Ryujinx runs deko3d NROs, which is how sa2-nx and
   banjo-nx test before the console. HowToDeko3d.md section 6 lists 14 deko3d/uam traps to check one by one (release
   library aborts silently: link `libdeko3dd` during bring-up; clip-space y up in 0.5.0; the tile-height bug of
   0.5.0; depth textures read (D,D,D,1); clears respect the scissor; uam loses a dynamic component index into a UBO
   vector ...).
2. **CPU**: bejeweled3-nx `optimising-functions.md`: start from a console profile (`switch_profile_stacks`), then
   localized copies of hot functions, write-backs of changed registers only, inlined small callees, direct dispatch,
   hand-written natives; every change exact (same stores, same registers, same calls, same rounding) and measured
   against the previous profile. Then LTO + PGO (sa2-nx `building.md`; renut-nx `pgo-lto.md`).
3. **Measuring** (nfsmw-nx `measuring.md`): only on the console at stock clocks; A/B inside one session by switching a
   setting every N seconds; a no-op control setting for the noise; NVK GPU timestamps x 1.627; look at slow-frame share,
   not only the mean.
4. **Console extras**: FPS/resolution for the SaltyNX overlay and ReverseNX-RT come from the NRO itself
   (`sdk/src/ui/switch_saltynx.cpp`, nfsmw-nx `console-overlays.md`); a TCP debug server (SwitchWakerHD
   `debug-server.md`: live log, SD files, input, screenshots, deploy-and-restart) is worth having once console runs start.

## Next

- Run this NRO where NVK works: the console (title takeover), or Ryujinx (not tried). In Eden it cannot pass device
  creation.
- Stage 4 on the PC: the native renderer (deko3d road), which also makes the emulator usable for tests.
