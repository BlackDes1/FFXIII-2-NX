# Final Fantasy XIII-2 for the Switch (ffxiii2-nx) on the katamari-nx base

Final Fantasy XIII-2, Europe disc (default.xex 0.0.0.11) + title update 6 (0.0.6.11), title 53510806, XDK
2.0.20871. The base is katamari-nx (github.com/ChanseyIsTheBest/katamari-nx at dcd7e290, 2026-11-02), itself banjo-nx /
renut-nx's tree with reNut's Direct3D-level renderer on plume's deko3d back end. Credits: reNut's renderer (its author
permits reuse in these ports), katamari-nx, renut-nx, sa2-nx, NetherSX2_nx, battd_nx, bejeweled3-nx and the zuma ports.
GPL-3.0 where the sources are.

The earlier tree of this port (Works\ffxiii2-nx, github.com/BlackDes1/FFXIII-2-NX, branches stage3-boot and switch-s1)
was built on sa2-nx's SDK with the emulated Xenos GPU. Its analysis, fixes and measurements carry over and are cited
here; its code stays where it is.

## Decisions (2026-11-03, made without asking, as the request said)

| What | Decision | Why |
|---|---|---|
| Port folder | `Works\ffxiii2-k` (the user's PC), `/home/claude/ffxiii2-k` (the cloud copy, local git only) | next to the earlier tree, which stays as the reference; no push without the user's say |
| Internal project name | `renut` (as katamari-nx, banjo-nx) | their CMake, post-codegen passes and renderer glue name `renut_*` files and targets; the NRO and SD folder become ffxiii2-nx in step 2 |
| `app/assets` | a junction to `Works\game_root` | the disc's files with the TU6 `.xexp` beside each `.xex` (the SDK applies them at codegen and at run time) |
| Zone modules | `[[modules]]` z0070/z0130/z0160/z0161 in `app/renut_manifest.toml`, code in `app/generated_zones/<module>` | all four load at 0x88800000; codegen once per module (`--target`), then `tools/ffxiii2_post_codegen_modules.py` (from the earlier tree) |
| Function boundaries | the earlier tree's analysis, every entry with its evidence: 337 entries of default.xex in `app/renut_config.toml`, the modules' in `app/config/ffxiii2/` | the crash-class audit of 2026-11-03 (earlier tree, `docs/crash-class-audit.md`) found none missing or wrong |
| Katamari's own content | removed: its `[functions]`, overrides (switch tables, indirect calls), hooks, hot / bulk / hand lists, indirect promotions; `pgo/` and `dist/` not copied | each was Beautiful Katamari's addresses ("Stale mentions", optimising-functions.md) |
| Codegen keys | katamari-nx's keys "that need no per-game audit" kept; the register diet (`non_volatile_as_local`, `elide_lr_stores`, `non_argument_as_local`) **off** | the diet needs this game's audits on code generated with it (katamari-notes.md, "Codegen options") |
| First boot | the SDK's emulated GPU (`gpu_plugin = "xenos"`), Renut compiled out (`RENUT=OFF`, katamari-nx's default) | separates CPU / kernel problems from renderer problems; Renut's hooks are at N&B's / Katamari's addresses and must be moved first |
| Host toolchain | `tools/pc_env.bat`: VS 2022 Build Tools, LLVM clang 20.1.8 in `Works\_tools\llvm` | this PC's (katamari-nx's names VS 18 and MSYS2 clang 22) |
| Game-specific fixes | `app/src/ffxiii2/` (README.md lists them) and two SDK files | the request: every game-specific fix in its own folder |

## The game

- default.xex: base 0x82000000, entry 0x82B65478 (xstart, TU6), image 0xF50000 patched; .text 0x82250000-0x82C312D0, BINK
  and RADCODE up to 0x82C452F4. Graphics interrupt handler sub_828510C8; Direct3D GPU wait sub_82856928 / sub_82850D88.
- XDK **2.0.20871**, the same as Zuma's Revenge (zumasrevenge-nx) and close to Sonic Adventure 2 (2.0.21076, sa2-nx).
  Far from N&B's title update (2.0.7978), which reNut's renderer was written for, and from Katamari's 2.0.5632. The
  Direct3D device layout and the hook signatures should be taken from the nearest XDK: sa2-nx's Renut at 2.0.21076,
  or zumasrevenge-nx at 2.0.20871 if its tree is available (docs/xdk-versions.md of renut-nx).
- Four zone DLLs (z0070, z0130, z0160, z0161), 7.6 GB of data in `alba_data` (largest file 1.9 GB).

## Direct3D anchors (census for step 2, from the earlier tree's generated code; not yet confirmed by signatures)

The callers of the video imports, the usual starting points for the device's functions (katamari-notes.md did the same
by hand for 40 hooks):

| Import | Called from | Likely |
|---|---|---|
| VdSwap, VdGetSystemCommandBuffer, VdPersistDisplay | sub_82858630 (its only caller of VdSwap) | Swap; its callers sub_8285A0A0 and sub_82888048 are Present candidates |
| VdInitializeRingBuffer, VdEnableRingBufferRPtrWriteBack, VdSetSystemCommandBufferGpuIdentifierAddress | sub_82852AF0 | the ring buffer set-up; called from sub_8285EF30 and sub_8285F388 |
| VdSetGraphicsInterruptCallback | sub_8285ED58, sub_8285EF30 | device creation / reset; sub_8285EF30 is called from sub_82856CA8 |
| (graphics interrupt handler) | sub_828510C8 | the callback itself (hooked by `app/src/ffxiii2/ffxiii2_irq.cpp`) |
| (GPU wait) | sub_82856928 / sub_82850D88 | the D3D block-until-idle path (the stage-3 profile) |

The Direct3D library sits around 0x82850000-0x82890000. With XDK 2.0.20871 the reference for signatures and device
offsets is sa2-nx's Renut (2.0.21076) or zumasrevenge-nx (2.0.20871); `tools/renut_sigmatch.py` needs that game's image
and generated tree, which this workspace does not have.

### Renut's hooks at FF's addresses (2026-11-03)

Reference: zumasrevenge-nx (Zuma's Revenge, XDK 2.0.20871). FF's Direct3D device has the same layout (Peggle 2's):
FF's ZEnable setter sub_8284A2C8 stores the depth-stencil shadow at 12308 = Bejeweled 3's 12284 + 24, as Zuma's Revenge.
So this tree takes zumasrevenge-nx's Direct3D layer (`app/src/renut_engine/gpu`, already at that layout) instead of
katamari-nx's (XDK 2.0.5632 layout); katamari-nx's SMAA and internal-resolution files are left out of gpu.cmake.

Guest data the layer reads, found in FF:

| Constant | Zuma's Revenge | FF | Evidence |
|---|---|---|---|
| kRenderStateTable | 0x82974578 | 0x82CBB508 | 101 rows {getter, setter, default} of D3D-area pointers; ZR's ZEnable + 0x53E8F0 = FF row 10's setter |
| kSamplerStateTable | 0x82974A38 | 0x82CBB9C8 | the 20 rows right after, as in ZR (0x4C0 apart in both) |
| kFormatTable | 0x8205EAA0 | 0x821469E8 | GetLevelLayout sub_8284DD50: `lis r11,-32236; addi r11,r11,27112` |
| kSkipExportNanVertex / 512Pixels | 0x829C4CDC / E0 | 0x82E9447C / 80 | the only word pair each read by one function, the two 0x158 apart (N&B's DoBeginExportNanVertex / DoEndExport512Pixels) |
| GetCurrentThreadId (REX_IMPORT) | sub_82605DB0 | sub_82B62FD0 | `lwz r11,256(r13); lwz r3,332(r11); blr` |
| XMemFree (REX_IMPORT) | sub_82605D80 | not found yet | |

Hook pairing (`tools/local/zr_ff_chain.py` -> out/zr_ff_chain.txt): a ZR hook's FF function is taken when a run of
consecutive ZR hooks lands on FF function starts with the same delta (chance about p^(run-1), p = 0.023).

| Group | Hooks | FF delta | Evidence |
|---|---|---|---|
| State object (render/sampler state getters and setters, stream source, ...) | 225 | +0x53E8F0 | run of 225; the state tables agree row by row |
| Render targets, viewport, surface info | 8 | +0x53E818 | run 8 |
| Texture layout helpers | 11 | +0x53E6E8 | run 11 |
| Shader constants, Set*Shader, vertex declaration | 9 | +0x536638 | run 9 |
| Ring buffer, fences, kick-off, EndIndexedVertices | 9 | +0x537DE0 | run 9; puts SetRingBufferParameters on sub_82852AF0, the VdInitializeRingBuffer caller found before |
| The rest (~40: Swap, Present, Clear, Resolve, draws, resources, device creation, ...) | | | runs of 6-7 into 0x8295xxxx conflict with each other: noise. Swap is sub_82858630 (its only VdSwap caller). Pairing by the PC trace (`src/ffxiii2/ffxiii2_d3d_trace.cpp`). |

A first run of the test gave false runs into the state object (its setters are all one size, so a shifted delta
lands on starts too); the state object is now excluded from the other hooks' candidates.

`tools/ffxiii2_renut_hook_remap.py` applies out/ff_hook_map.txt to the hook files (unmapped hooks become
BANJO_RENUT_UNHOOKED_*).

## Fixes carried over (`app/src/ffxiii2/README.md`)

1. The graphics interrupt handler without the runtime's global mutex (`ffxiii2_irq.cpp`): stage 3 froze after the first
   button press in 2 of 6 PC runs without it, 0 of 6 with it.
2. `XamUserWriteProfileSettings` stores the value types (SDK `xam_user.cpp`, the earlier tree's patch 0001).
3. `readback_memexport = false`: stage 3's ~100 s frozen loading screen (one GPU readback per memexport draw). Katamari's
   built-in Switch config already has it; the PC runs pass it.
4. `switch_guest_views_eager` (SDK `guest_memory_switch.cpp`, patch 0002): Eden delivers no data aborts.
5. No `std::thread::detach()` in this game's code: the console run of 2026-11-03 (earlier tree, NVK build) ended at start
   with `std::terminate: std::system_error` in `std::thread::detach`, called by the frame-capture thread from
   `OnPostSetup` (addr2line on the build's ELF). Persistent threads instead (nfsmw-nx platform-notes.md, "Threads").

## Renderer census (porting-guide-renderer.md section 2), 2026-11-04, PC (Renut, Vulkan)

What is known so far; the census keys (`banjo_renut_diag_fast_census`, the Unleashed census, `banjo_renut_diag_shadow_census`)
are still to be run on the title screen and in gameplay, then on the console.

1. **Where draws come from:** direct D3D calls into the five DRAW_INDX builders (sub_82857080, sub_8285F618,
   sub_8285FB20, sub_82860100, sub_82860518), hooked; no recorded command buffers seen yet. So the record-time items
   (4.5-4.7) do not apply as in N&B until a census says otherwise.
2. **Where resources come from:** the engine builds its own: shaders and vertex declarations (sub_82890620 /
   sub_828492F0), surface headers on a thread's stack (bound as heap copies, `ffxiii2_renut_stable_stack_surfaces`),
   the UI atlas built by the GPU. Section 4.6's bind-time objects are the fitting kind.
3. **How state is set:** through D3D setters, with FF's own set-all-targets sub_8284CE30 (hooked); inline writes not
   counted yet.
4. **Memexport:** 17 point-list draws at start-up only (the font/UI atlas, VS 6549C96C96C2721F, 32 MB window at
   0x0CBA0000); none per frame on the title screen.
5. **Shadow passes:** not reached yet (title screen only).
6. **Threads:** PC Renut steady 30 fps on the title; console numbers only from the earlier NVK tree (8.8-11.6 fps,
   GPU-bound).
7. **Where the shaders are:** the engine registers them at run time from its own packages (106 captured on the PC,
   `app/out/pc/shader_capture` → `out/renut_shader_dump`); the Switch build needs every shader a run reaches captured
   on the PC first (or the release package made from the player.s files).

## Hook map corrections (2026-11-04)

The N&B → FF remap (out/ff_hook_map.txt) placed two getters on FF functions that are not getters:
- sub_8284C080 is **SetIndices** (writes the index buffer word at device +0x320C): hooked as SetIndices now (it was
  GetStreamSource, whose writes through r5/r6 crashed the game when it left the title screen).
- sub_8284C198 (mapped as GetRenderTarget) builds something in a 1648-byte frame and copies it to device +15408:
  unhooked.
Every remaining `D3DDevice_Get*`/inline-output hook should be checked the same way before the Switch build (the
generated code of the hooked address against the handler's arguments).

## Testing without a person at the pad

`--input_script=<s>:<button>,...` (SDK, input_system.cpp): scripted presses for user 0, a pad always connected, works
minimized and in Eden through the toml. The PC runs use `20:A,24:START,28:A,32:A,36:A,...` to leave the title screen.
Keyboard presses sent to the window do not reach the game unless `mnk_mode = true` (the SDK's keyboard pad).

## Switch build (2026-11-04)

- deko3d tools: SPIRV-Cross was fetched with the user's permission; `out/deko3d_tools` built, `tools/switch_shaders.ps1`
  compiled 624 shaders. NVK is not linked (`REXGLUE_SWITCH_NVK` OFF).
- Build: `powershell -File tools\ffxiii2_switch_job.ps1 [-Reconfigure]` (VS Build Tools' CMake and Ninja through
  sa2-nx's `tools\win\env.ps1`; log `out\switch_build.log`). Output `app\out\switch\renut.nro`; on the SD it goes to
  `/switch/ffxiii2-nx/ffxiii2.nro` with `game_root` (the disc) next to it and the title update at
  `/switch/ffxiii2-nx/update`.
- Zone modules (z0070/z0130/z0160/z0161) are linked into the NRO: an NRO cannot load libraries. Each module's static
  archive is partially linked, its hidden symbols made local and its two entry points renamed; 
  `src/ffxiii2/ffxiii2_static_modules.cpp` registers them under `generated/module_registry.cpp`'s names and
  `KernelState::LoadUserModule` looks there first (`rex/system/static_modules.h`).
- Settings: the built-in `app/ffxiii2_switch_defaults.toml` (PC defaults + deko3d backend, quiet log) and the player's
  `ffxiii2.toml` next to the NRO (from `app/ffxiii2_switch.toml`). Test runs add keys there (log, `input_script`,
  captures).

## What is missing for the work order (2026-11-04, end of the hour)

- **Step 2-3 (deko3d, Eden):** first Switch build running (above); then Eden with `input_script` in `ffxiii2.toml`.
- **Step 4 (enhancements):** the base's keys are on as Katamari shipped them; not yet A/B-checked on this game. The
  Katamari hand natives (`katamari_hand_*`) and the banjo hand natives install only through hot-list hooks, which are
  empty (`app/src/cpu/hot/*.txt`), so they do nothing in this game yet; their keys still print ON in the
  enhancements line.
- **Step 5 (hot functions):** no profile of this game yet (PC profiler run and Eden profile pending; no console log of
  the new tree).
- **Step 6 (PGO/LTO):** waits for the user.
- **Step 7 (console package):** waits for the NRO.
- Remaining Katamari material: `src/katamari_keys.cpp`, `src/cpu/katamari_stage_diag.cpp`, the `KATAMARI_*` CMake/macro
  names (`KATAMARI_BUILTIN_CONFIG`, `KATAMARI_RENUT_HOOKS`, `katamari_builtin_config.h.in`), the old
  `app/katamari_switch*.toml` (no longer read), the DLC unlock key.

## Emulator: Ryujinx (2026-11-04, the user's decision; Eden no longer used)

- `tools/ryujinx_run.ps1 -Name <run> [-Seconds N] [-Toml tools\ryujinx\ffxiii2_ryujinx.toml]` runs
  `Port\ryujinx\Ryujinx.exe` (1.3.3, user-profile mode: keys and config in `%APPDATA%\Ryujinx`). This port's SD folder
  is `%APPDATA%\Ryujinx\sdcard\switch\ffxiii2-nx` (NRO, `ffxiii2.toml`, junctions `game_root` and `update` to the PC's
  game data). One run at a time (not started while any Ryujinx runs, nor under 8 GB free commit); only its own process
  is stopped; Ryujinx's log of the run, the SDK's logs, deko3d.log and the captures go to `out/ryujinx_runs/<run>`.
- Ryujinx starts an NRO without a loader: the SDK uses `/switch/ffxiii2-nx/ffxiii2.nro` as its path and maps guest views
  eagerly by itself. Ryujinx 1.3.3 refuses the pseudo process handle (r1: every commit 0xE401, crash at 7.7 s), so the
  SDK now makes a real own-process handle (nx-hbloader's method, `Proc()` in guest_memory_switch.cpp; a console with the
  loader's handle is unchanged). r2: that exchange hung; the fix (reply before close) is not tested yet.
- Why not Eden (kept for the record): Eden refuses the pseudo process handle in `svcMapProcessMemory` (0xE401), so no
  guest view was ever mapped (k1-k3: `Unmapped Write` from 7 s). A fix is kept unapplied in
  `tools/sdk_patches_unapplied/eden-own-process-handle.patch`.

## PC bugs reported 2026-11-04

### Main menu audio
- Measured (tile1, `audio_dump_s` / `audio_dump_output_s` from 20 s for 40 s): the game's 6-channel mix and the SDL
  device output both carry the title and main-menu music from 22 s to 48 s (RMS 0.01-0.05, peaks 0.1-0.3; the capture at
  40 s is the main menu's "Begin game in NORMAL MODE?"), fading out when the menu is left for the save screen.
- Cause of the silent runs: `tools/run_pc.ps1` passed `--audio_mute=true` to every scripted run (play8, whose capture
  is in the report, among them). There is no settings file next to `renut.exe`, so a manual start is not muted. Fixed in
  the script: not muted unless `-Mute`. **Verified: the user hears the menu music (2026-11-04).**

### Battle image (captura_013_0260s.png vs rightrender.png)
Three causes, found one after another (runs tile1, dl1, tile2; draw log with viewport/scissor/tiles and the resolve
trace at the battle):
1. **Predicated tiling unknown to the renderer.** FF draws the 3D scene in two side-by-side tiles (640x720 each;
   2x MSAA 1280x720 does not fit the EDRAM); `D3DDevice_BeginTiling` / `EndTiling` were unhooked (the hook map came
   from Zuma's Revenge, which has no tiling). FF's are `sub_8285CC20` / `sub_8285CF90` (found by the PM4 bin
   mask/select packets and their callers; the tile count/rects at device +0x3344/+0x3348). Hooked as **pass-through**:
   FF's own functions run, the renderer is told the tile rects and the end. (N&B's full native replacements were tried
   first, tile1: they rewrote FF's device state, and the tiled pass lost its depth target and depth test, so the sky
   covered Chaos Bahamut.)
2. **Each tile's resolve read the left tile.** FF resolves per tile with the source rect in the tile's own EDRAM
   coordinates ((0,0)-(640,720) for both) and the destination at the tile's corner ((0,0), (640,0)); the host keeps
   the whole screen in one target, so the second tile's resolve copied the left half again: the doubled scene and the
   seam. `ResolveStateLocked` now moves a tiled resolve's source to its tile (draw.cpp).
3. **The game's display gamma ramp not applied** (the remaining darkness: sky RGB ~7/13/14 against the reference's
   ~39/56/56, a power of about 1.8-2.2). FF sets a display ramp (the console's scan-out applies it); its
   `SetGammaRamp` / `SetPWLGamma` were unhooked: `sub_8284C198` / `sub_8284C268` (0xD0 apart like N&B's pair; each
   stores its corrected 1536-byte ramp at device +0x3C30; sub_8284C198 had been mistaken for GetRenderTarget).
   Hooked as pass-through; the presenter applies the stored ramp (`zumasrevenge_renut_display_ramp`, on: the
   console's behaviour, not a picture change).

Status: after 1 and 2 (tile2) the scene is one image with the sky, Chaos Bahamut and the HUD where the reference has
them, no seam. 3 turned out not to matter for brightness: FF's ramp is the identity. Remaining difference: the scene
is darker than the YouTube frame, but the SDK's **emulated Xbox 360 GPU** (the game's own command stream) is about as
dark at the same battle (sky 23/35/39, mean 25.9; Renut 15/22/25, mean 24.5 a moment earlier). All of FF's colour
targets are 8_8_8_8 (no gamma targets). So the gap to the video is most likely the video's capture/brightness; a
same-frame Renut vs emulated comparison is still to do for the residual.

## Hot functions (work order 5.5), state 2026-11-04

- First profiles of FF: **Ryujinx r8** (`switch_profile_stacks = true`; without argv[0] the SDK writes
  `sdmc:/switch/rex_profile.log`, which `tools/ryujinx_run.ps1` now collects as `sd_rex_profile.log`; ELF kept as
  `out/ryujinx_runs/r8/renut.elf`) and **PC prof1** (`banjo_pc_profile_s=10`, `app/out/pc/logs/pc_profile.log`, copied to
  `out/pc_runs/prof1`). Ryujinx serves the SDK's thread pausing unevenly: the effect thread `vfx-qix-effect` got 41k
  samples, the main thread 168.
- **First batch: 177 functions** in `app/src/cpu/hot/localized_bulk.txt`: 49 from r8 (`tools/switch_hot_bulk.py ... --min-samples
  1 --merge --write`) and 128 from prof1 (`tools/pc_hot_bulk.py`, new: the same filters on a PC profile, min 20 samples).
  Left out by the rules: reserved (atomic) guest accesses (8, among them the effect thread's three hottest:
  sub_828B8310, sub_828BADA0, sub_828B8298), tiny leaves, host calls, a time-base read.
- Stale mentions: `ffxiii2_d3d_trace_hooks.inc` (the D3D call trace, a diagnostic build only: `-DFFXIII2_D3D_TRACE=ON`)
  names ~7,000 addresses and blocked 21 of r8's hot functions. `localize_native.py`'s `Hooks` skips it now. **A trace
  build must be made with the hot lists emptied** (it hooks the same functions; the link would fail). Otherwise
  negligible for FF (3 functions behind `BANJO_RENUT_UNHOOKED_*` placeholder lines).
- Making the copies: on the PC, codegen + `tools/post_codegen.py` (`localize_native.py --list --bulk` runs in its
  order), then `tools/run_pc_verify.ps1` (the guard on every call: 0 differences and the counts showing each copy was
  reached), then the Switch build. **verify1: 130 of 177 reached, 64.3 M calls, 0 differences; verify2 (through the battle): 177 of 177
  reached, 118.4 M calls compared, 0 differences** (one call differed once and not when repeated: nondeterminism, as the
  guard classes it). Batch 1 verified.
- Emulator and PC profiles qualify machine copies only, never hand-written natives.
- **Ryujinx r10** (Switch build with batch 1): runs like r8 (no regression in the emulator; performance is judged on the console). ELF 82.7 MB (78.3 MB without copies). The effect thread's hottest function `sub_828B8310` is a counting spin barrier (atomic decrement at +32, last arriver resets +36, the others spin): the first hand-written candidate for the console profile (waiting instead of spinning removes work), not written before a console profile names it.
- **Batch 2:** 32 functions from r10's profile (211 listed, 209 made). **verify3: 209 of 209 reached, 127.6 M calls, 0 differences.** Switch build 6 min, NRO 65.7 MB (+0.25 MB), ELF 83.0 MB; Ryujinx r11 normal, heap 3,202 MB free.
- **Build pitfalls found:** (1) the build's own codegen step declares only its stamp as an output, so ninja never rebuilds the PCH after it rewrites renut_pch.h (clang refuses the objects; GCC would use the stale PCH silently): both job scripts touch cmake_pch.hxx when the header is newer, and the PC job builds once more after a stale-PCH failure. (2) The PC build relinks the generator and so re-runs codegen itself after a job's explicit codegen (16 min twice). (3) The job runner strips double quotes from job lines. (4) device_commit_files may reuse an earlier upload of the same staged path: commit jobs from uniquely named files.
- **Console package rc1** (work order 5.7): `tools/package_ffxiii2.ps1`, `dist/switch/ffxiii2-nx/` (renut.toml, renut_play.toml, renut_ab.toml with target 10 as the do-nothing control and 30 s halves, renut_profile.toml, README.txt), notes `docs/package_notes/rc1.txt`; made as `Works\builds\2026-11-05_0642_ffxiii2_rc1`.

## Status

See `docs/progress-log.md`.
