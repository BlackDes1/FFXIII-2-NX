# 30 fps at the default handheld clocks (work order 2026-11-07)

Console: CPU 1020 MHz, GPU 460.8 MHz, memory 1331.2 MHz, handheld (the official profile). Target: a steady 30 fps
(two-blank swap: a frame must fit 33.3 ms on every unit).

## Measurements (console, scripted battle: the opening fight, Auto-battle every 2.5 s)

| Run | Build / config | Battle fps (10 s blocks) | GPU ms a frame | Main thread |
|---|---|---|---|---|
| k245a | k244, rc8 + `renut_gpu_profile` | 15-24 (opening scene 16) | 33-40 (opening scene 54-61) | - |
| k246a | k246, dynamic resolution test cycle 720/612/540 every 20 s + profile | 18-24 at every height | 720: 35-38, 612: 28-31, 540: 23-27 (opening scene 60 -> 41 at 540) | 89 % of a core |
| k246b | k246, dynamic resolution live + `switch_profile_stacks` | mean 21.9 (22 blocks) | mean 29.0 | 90 % of a core |
| k246c | k246, rc8 toml (dynamic resolution off, 720) | mean 19.6 (22 blocks) | mean 39.1 | - |

k245a GPU split (battle, 16 blocks, 36.4 ms): 31.5 ms in 1280x720 targets (draws 26.4, colour resolves 2.6, MSAA
resolves 0.5, depth resolves 1.0 + 1.6 shadows); heaviest single passes PS 0938773DF7F7FA28 (4 ms, one opaque draw,
4 fetches incl. a cube map), FCF76CCFFD55FFDE (smoke, 2-6.5 ms, 100-300 draws), 870B06B2245CD010 (2.2 ms, full
screen), 62627B409EEF3BF1 (1.5 ms). Opening scene: PS AEAFDCD8198EDD3F 14 ms (one full-screen draw: a 32-tap radial
blur of an RGBA16F target).

## What the data says

1. GPU: dynamic resolution brings the battle from about 36-39 ms to 24-30 ms a frame (k246b vs k246c).
2. CPU: with the GPU under budget the game still runs at 17-25 fps: the game's main thread is busy about 90 % of a
   core (about 40 ms a frame). Its profile is flat (k246b, 4,142 samples in battle): 53 % recompiled game code
   (machine copies and natives included), 29 % host code (Renut's D3D layer on the main thread: CaptureDelta 2.2 %,
   SetStreamSource, SetTexture, QueueDraw, Push, Ffxiii2StableSurface), 12 % SDK, 6 % C library; the top function
   sub_829FFA58 is 4.6 %, then sub_828AABF0 2.7 %, sub_829F14D8 2.5 %.
3. So 30 fps needs both: dynamic resolution (done) and about 25 % less main-thread work.

## Adopted

- **Dynamic resolution** (`banjo_renut_dynamic_resolution`, off by default; NOT pixel-identical, allowed by the user
  2026-11-07). Source of the design: renut-nx (ChanseyIsTheBest/renut-nx docs/enhancements-reference.md "Dynamic
  resolution"). Our version: three fixed levels 720 / `_handheld_mid` (612) / `_handheld_low` (540) on render scale
  codes 0 / 4 / 6, each code's height set once a session (render_scale.h), the GLSL package's existing scalecodes pass
  reads the heights from SharedConstants (draw.cpp `dynamic_heights`, v[84].w). Controller (surfaces.cpp): 16-frame
  GPU average against `_gpu_ms` (30), down to the level predicted to fit (`_scaled_share` 0.85 of the time scales with
  pixels), up after `_up_s` (2 s) with the level above predicted under 85 %, 4 frames ignored after a change; shadow
  maps stay at 1x. Overlay (L3+R3): render height, GPU average, frames per height. 10 s log line `[renut] dynamic
  resolution`. Test key `_test_s` cycles the levels.
- **PGO + LTO** (allowed by the user 2026-11-07): the existing sa2-nx / banjo-nx machinery (app/CMakeLists.txt
  `BANJO_PGO`, `BANJO_SWITCH_LTO`, src/banjo_pgo.cpp, tools/pgo_merge.py); build folders app/out/switch-pgo-gen and
  app/out/switch-pgo-use; tools/ffxiii2_switch_job.ps1 -BuildDir / -Extra / -Log, tools/switch_tests/perf_run.ps1
  -Nro / -Elf.

## PGO + LTO and four cores (console, battle, dynamic resolution on, 180 s each)

| Run | Build | Battle fps | GPU ms | Render thread ms | Main thread |
|---|---|---|---|---|---|
| k247b | k246 everyday | 21.6 | 28.8 | 11.0 | ~87 % |
| k247u | PGO (1 training battle) + LTO | 24.0 | 27.6 | 9.9 | ~86 % |
| k247c | PGO + LTO, banjo_system_core 2 + XMA on core 3 | 24.4 | 27.6 | 10.0 | ~92 % (core 3 idle 50 -> 31 %, one 10 s block with 59 audio requests without data) |

### k250: the optimised build (300 s battles, renut_30fps_test.toml, the last 34 10-s blocks)

| Run | Build | Battle fps | Lowest block | Last 2 min |
|---|---|---|---|---|
| k250t | PGO training NRO (instrumented, copies + ABI sync + hand native + compile core 3) | ~19 | 11.7 | 26-30 |
| k250u | k247 folder rebuilt with the k250 profile (mixed profile) + LTO | 26.0 | 17.3 | 28.0-30.0 |
| k250v | fresh folder app/out/switch-pgo-use2, k250 profile only + LTO | **26.7** | 19.7 | 29.4-30.0 |

k250v is the build on the Switch (current slot, ffxiii2_30fps.toml, 2026-11-07): +11 % over k247u (24.0), +36 % over
the dynamic-resolution-off rc8 configuration (19.6). Steady 30 fps in the later part of the fight; the heavy phases
(many enemies and effects, 100-135 k draws per 10 s) still drop to 20-25: the main thread, not the GPU (540 lines,
GPU 24-27 ms).

**Crash found and fixed on the way (k249t):** the instrumented NRO stopped about one second into the game with a guest
read of 0xFFFFFFFF (r25 = r30 = FFFFFFFF, lr 82B572E0, thread "CDevCommon Task1" being created). Cause: item 8's
ABI sync in tools/localize_native.py kept r14-r31 / f14-f31 in the copy's locals across EVERY unknown-reach call,
including the compiler's register save / restore helpers (`__savegprlr_N`, `__restgprlr_N`, `__savefpr_N`,
`__restfpr_N`, vmx). `__restgprlr_14` loads r14-r31 from the stack into the context: the copy of sub_82B571D0 (a new
bulk entry) did not load them back and wrote its own working values to the context at its return, so its caller got
FFFFFFFF in r25 / r30. Fix: those helpers get an explicit reach (every register of the header) so the sync rule does
not apply to them (`ABI_HELPER` in localize_native.py). After the fix: k250t, k250u, k250v without a crash, 0 access
violations, 0 audio underruns. The PC verify of k249 did not catch it because that function was not reached on the
PC battle path before the guard's comparison window.

39-bit address space: already used (the guest window is one 0x120000000 reservation with its mirrors; every guest
access is base + zero-extended address, `REX_LOAD_*` in renut_pch.h). Nothing left to gain there.

## The 20 CPU optimisations (user request 2026-11-07; from the overlay, perfil_pilas / switch_profile_stacks runs
k246b and k247s, tools/switch_hot_symbols.py and tools/switch_hot_bulk.py with the exact ELFs)

Main thread in battle (k247s, PGO build, 7,624 samples with k246b): 48 % generated code without a copy, 32 % copies,
17 % host code, 4 % hooked. Every item is measured on the console before it is kept.

| # | Optimisation | Evidence | Status |
|---|---|---|---|
| 1 | PGO + LTO (sa2-nx / banjo-nx machinery) | +11 % (21.6 -> 24.0 fps) | done (k247u) |
| 2 | Dynamic resolution (GPU side) | GPU 39 -> 28 ms | done (k246) |
| 3 | Bulk native copies of the battle's uncopied generated functions (tools/switch_hot_bulk.py, bejeweled3-nx's method) | 48 % of the main thread is uncopied generated code | done: 211 -> 547 entries, 541 copies (k248) |
| 4 | DKSH cache with the battle's run-time shader variants (no uam compiles in play) | thread on core 2: 22 % of all samples, 27 % of it in nv50_ir (uam) | the shipped build uses rc9's cache; the console adds the variants it compiles to it (kept across sessions) |
| 5 | Background shader compiles on core 3 (banjo_background_compile_core, range now -1..3) | same thread shares cores 0-2 with the main thread | done (ffxiii2_30fps.toml) |
| 6 | PGO re-trained after 3 / 7-11 (one 300 s battle with every copy in) | the k247 profile predated the copies | done (k250, 26.7 fps); more scenes still planned |
| 7 | Hand native: 4x4 matrix multiply sub_8228EFC0 (writes 17 registers instead of 31 vector registers per call) | 1.4 % of the main thread, called by the top copies | done (hand.txt) |
| 8 | Fewer register hand-overs in big copies: ABI sync (r14-r31 / f14-f31 kept in locals across unknown calls) | up to 81 registers stored / loaded around real calls | done, with the k250 save/restore-helper fix |
| 9 | sub_829FFA58 (hand native or fewer hand-overs) | 4.9 % | covered by 8 |
| 10 | sub_829F14D8 | 3.2 % | covered by 8 |
| 11 | sub_828AABF0 (also 2.3 % of the audio thread) | 3.0 % | covered by 8 |
| 12 | Renut CaptureDelta: compare only chunks the D3D setters marked (dirty bits) instead of every chunk | 2.5 % (host) | next |
| 13 | memcpy on the main thread (callers to find) | 1.1 % | planned |
| 14 | Guest critical sections / mutexes: uncontended fast path | RtlEnterCriticalSection + mutexLock/Unlock ~2 % | planned |
| 15 | ObjectTable::LookupObject: handle cache | 0.6 % | planned |
| 16 | XThread::CheckTitleTermination polled per call | 0.3 % | planned |
| 17 | XAudio mixer natives (sub_82BB4008, sub_82A34BB0: 31 % of the audio thread) | frees a shared core | planned |
| 18 | -fipa-pta at the LTO link (BANJO_SWITCH_IPA_PTA) | A/B | planned |
| 19 | Inliner limits with the profile (BANJO_SWITCH_INLINE_AUTO / _UNIT_GROWTH) | A/B | planned |
| 20 | Four-core layout (game threads on core 3, XMA kept on cores 0-2) | +0.4 fps, audio risk with XMA on core 3 | re-test after 4-5 |

## Open

- k246b's last screenshot (540 lines, after the fight) shows Lightning on a floor of coloured blocks; the log has no
  warning. Not decided: needs the same scene at 720 (Frame Tap) before calling it a dynamic-resolution bug.
- The HUD is drawn at the lowered height (softer text). katamari-nx's ui_scale.cpp (HUD at display resolution) is in
  the tree but not built; it needs FFXIII-2's final composite identified.
- References not read (rate limits / robots): SwitchWakerHD docs folder (robots), renut-nx renut-renderer-port.md and
  pgo-lto.md (rate limited twice).
