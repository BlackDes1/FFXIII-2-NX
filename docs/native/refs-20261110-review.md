# katamari-nx / renut-nx (downloaded 2026-11-10): docs read, optimisations applied, FSR 1

## Download
Port\refs_20261110\ (zip archives from GitHub, extracted; no build, no install):
- katamari-nx-main.zip 103,958,149 bytes (sha256 08E6E7FD1D35FC6C...), docs/ 9 files.
- renut-nx-master.zip 105,769,800 bytes (sha256 09FEBAD89D4A9419...), docs/ 20 files.

## What the docs add over the 11-04 copies this port was built from
Every doc compared line by line with Port\katamari-nx-main and Port\renut-nx-master (the 11-04 copies):
- renut-nx: deko3d-renderer.md section 43 (renut-nx 1.0.2, 2026-11-05) and two lines of enhancements-reference.md; the
  other 18 docs are unchanged. katamari-nx: katamari-notes.md "1.0.1: 2 MB blocks and the boot's cvars" and one
  progress-log row; the other 7 unchanged.
- New items: (1) game memory on 2 MB blocks (banjo_window_blocks: already here as ffns_window_blocks, -3.4 ms);
  (2) the NRO's code and read-only data on 2 MB blocks (banjo_code_blocks, sdk/src/ui/switch_code_blocks.cpp, from
  kameo-nx / the Torchlight port: 2-4 % there, 4.3 % in Kameo); (3) the boot's cvars (cvar::Init registers only the
  cvars the command line names: CLI11 is quadratic in their number); (4) banjo_tlb_test_s (a TLB cost probe: not taken);
  (5) rex_version.cmake no longer reads the repository's tags (not relevant here).
- A check of every cvar name in both repos' docs against this tree: the names not here are diagnostics/A-B keys,
  Katamari-specific native functions (banjo_cpu_native_* at Katamari's addresses), renut-nx's newer draw path (record-time
  draw path, replay stream, fetch cache: the render thread, not this game's limit) or the same items under this tree's
  sa2_ / ffxiii2_ names (snapshot deltas etc.).

## Applied (tools/build/renut102_patch.py; backups *.before_ffns_r102)
| Item | Where | Default | Console (S1b, handheld) |
|---|---|---|---|
| NRO code + read-only data on 2 MB blocks | sdk/src/ui/switch_code_blocks.cpp (renut-nx's, key ffns_code_blocks, read from ffxiii2.toml), rexglue_switch.cmake, guest_memory_switch.cpp (cvar), memory_switch.cpp (status line) | on | log: "text 49584 KB and read-only data 12684 KB now on a copy in line with 2 MB blocks: room for 23 and 5 blocks"; ABBA cb01-cb04: on 20.25 / 19.29 fps, off 19.39 / 18.53 fps: **+0.81 fps (~4.3 %), -2.1 ms a frame**. Exact. Cost ~62 MB of heap (lowest free in S1b 1,144 MB) |
| Boot cvars | sdk/src/core/cvar.cpp | on | start-up only; the console log opens after Init, so not measurable there |

## FSR 1 in the options menu (tools/build/fsr/, fsr_patch.py; backups *.before_ffns_fsr)
With dynamic resolution FF XIII-2's whole frame (HUD included) is drawn at 1088x612 and the scan-out scales it to
1280x720 with one bilinear sample a pixel. New: render/fsr.cpp + shaders/host/fsr_easu_ps.hlsl / fsr_rcas_ps.hlsl (ports
of SwitchWaker's MIT shaders, which port AMD FidelityFX FSR 1.0's ffx_fsr1.h, MIT), wired into backend/present.cpp:
the upscale into a 1280x720 target before the swapchain's pass, then RCAS sharpening + the display ramp as the scan-out.
Key ffns_fsr: 0 bilinear (default, as before), 1 bilinear + RCAS, 2 FSR 1 (EASU + RCAS); ffns_fsr_sharpness in hundredths
of a stop (50; 20 sharper). Menu line "Upscaling": Bilinear / Bilinear + sharpen / FSR 1 / FSR 1 sharp (at once).
NOT pixel-identical above 0 (a player's choice). No effect at 720p.
