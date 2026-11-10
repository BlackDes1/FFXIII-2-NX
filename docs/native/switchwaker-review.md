# SwitchWaker docs review for FFXIII-2-Native-Switch (2026-11-10)

Source: Port\SwitchWaker-main\docs (19 files, ~1 MB): a decompilation-based GameCube port (Aurora/Dawn, later its own
deko3d renderer). Read for what applies to a static recompilation whose main thread is the limit (S1b: ~44 ms of jobs
per frame on the main thread, GPU 27.9 ms at 612p, render thread ~14 ms).

## Already in our port (same deko3d lineage: SwitchWakerHD -> banjo-nx -> our Renut/plume)
| SwitchWaker lesson | Here |
|---|---|
| Tegra GPU timer reads 1.63x low under GL/nouveau (PTIMER 19.2 MHz) | deko3d `dkTimestampToNs` (plume_deko3d.cpp:873): already real time |
| Official handheld profile 0x92220008 (GPU 460.8 MHz, EMC 1331.2, CPU 1020) | `banjo_gpu_boost` = true (banjo_enhancements.cpp, sdk switch_apm.cpp) |
| Precompiled DKSH cache, uam patches (no dual issue, block stall) | full shader cache (DKSH tag uam110-sw8-bnx4) |
| Zcull A/B; invalidate after copies into depth | `banjo_deko3d_zcull` = true, `invalidateZcullAfterCopy` |
| Hardware compression on colour targets | plume_deko3d.cpp:612 |
| Small writes to uncached memory are slow (40-byte copies 160 ms vs 2.5 ms per 8 MiB) | upload/readback buffers CPU-cached (HOST_CACHED) |
| Horizon does not time-slice equal priorities; keep helpers off the worker's core | P3 placement work (XMA on core 3 etc.) |
| Frame limiter drift (pace from the last wake, not the last wait's end) | display_sync.cpp: display-grid release, a late frame goes at once |
| Per-frame memo of pipeline lookups / bind groups on the FIFO worker | render-thread side; our render thread is not the limit |

## Not applicable
OpenGL/Mesa/Dawn fixes (deferred deletes, UBO window, GL state cache, Mesa shader cache), 60 fps interpolation
(FPS60_PLAN), HD textures, QoL/button remap/PAL disc/auto-update/runtime assets/widescreen/Windows notes, the
decompilation organisation docs.

## New candidates
1. **clang for the generated game code** (SWITCH_BUILD.md: SwitchWaker's game units compile with clang for the
   Cortex-A57; the SDK/libnx with devkitA64 GCC). Ours: renut_recomp (175 files) + zones z0070/z0130/z0160/z0161, 241 MB
   of generated C++, GCC 16.1 -O3. Exact in intent (same code, other compiler); main thread ~83 % generated code.
   Traps to check: `-mtp=soft` TLS (clang reads tpidr_el0 directly), `-ffp-contract=off` (clang's C++ default is on),
   libstdc++ 16 headers under clang, PCH. Needs an LLVM toolchain: none on the PC (VS has only clang-format/tidy).
   **Blocked: downloading LLVM 23.1.5 (900 MB, Apache-2.0) into tools\llvm needs the user's explicit OK.**
2. Lazy barriers (hazard epochs; SwitchWakerHD 47 -> 20 per frame): GPU side, worth it once the CPU is under 33 ms.
3. FSR 1 / area rescale for the dynamic-resolution picture (player option, not exact): quality at 612p/540p.
