# Performance — targets, historical baseline, measurement protocol

## Target
Nintendo Switch, handheld, ARM Cortex-A57 at 1020 MHz (GPU 460.8 MHz, EMC 1331.2 MHz), no overclock. Stable 30 fps
(two-blank swap): every frame ≤ 33.33 ms. Pixel-identical rendering at the game's native 1280×720 (2×MSAA scene).

Working budgets (ESTIMATE, to be refined): game main thread ≤ 28 ms CPU/frame; GPU ≤ 31 ms/frame; render thread
≤ 20 ms/frame on its own core.

## Historical baseline (old project, NOT this project's measurement)
See `previous-work-analysis.md` §2. Key rows: heavy field 14-17 fps (k257); battle 19.6 fps at 720p (k246c, GPU 39.1 ms);
main thread 30-45 ms in heavy phases; profile flat (1,391 functions, top 10 = 10.5 %).

First own measurement (2026-11-08, console A/B, details prototypes/P1_abi_lifted_codegen/RESULTS.md): scene S1b =
New Bodhum at night from the console's Save Data 01, no input, 120 s, clocks 1020 / 460.8 / 1331.2 MHz handheld.

| build | fps | ms/frame | core-ms/frame (rex_profile CPU total) | GPU ms/frame | render thread ms/frame |
|---|---|---|---|---|---|
| k263 config (old project's codegen), copies off | 10.36 / 10.76 | 94.7 | 228.5 / 220.7 | 35.3 | 15.0 |
| P1a-r1v2 (this project), copies off | 15.29 / 16.84 | 62.2 | 157.7 / 140.4 | 34.4 | 14.5 |
| console live build (old project, its toml, copies ON) | 12.91 / 14.00 | 74.3 | 183.9 / 162.0 | 34.8 | 14.3 |
| P1a-r1v2, second pair (vs live) | 15.74 / 15.80 | 63.4 | 150.5 / 147.0 | 34.7 | 14.9 |

Scene noise in S1b: draws per frame vary 450-484 between runs and frame time follows them (0.14 ms per draw/frame); the same binary ran 16.06 fps one evening and 16.79 fps the next night. Compare within one session, alternate the order, normalise by draws/frame (prototypes/P2_postpass_ports/RESULTS.md).

Next limits in S1b: the guest CPU still ~62 ms/frame; the GPU 34 ms/frame alone is above the 33.33 ms budget.

## Protocol (adopted from the old project's lessons)
- Clocks: reject any run whose header line is not 1020 / 460.8 / 1331.2 MHz handheld.
- Scenes: (S1) New Bodhum field route from a fixed save, (S2) the scripted opening battle (Auto-battle every 2.5 s,
  300 s), (S3) the opening real-time cutscene (GPU stress), (S4) a menu/loading sequence.
- Compile-time changes: two sessions per side, same scene; compare heavy-phase 10-s blocks with heavy-phase blocks.
  Run-time switchable changes: alternating A/B in one session (5-s halves; random encounters made two-run comparisons
  show +29 % that was not real).
- Report for every A/B: main-thread ms/frame, **total core-ms/frame** (removed work lowers both; moved work lowers only
  the first), fps mean / worst block / 1 % low where meaningful, render-thread ms, GPU ms.
- Overlay costs ~3 % (k261): keep it on in both halves or off in both.
- Tools: rex profiler 10-s blocks (`rex_profile.log`), 1 ms stack sampler (`switch_profile_stacks`) + symbolizer with
  the exact ELF, `renut_gpu_profile`, renderer read-back for frame truth.
- PC/Ryujinx: correctness only; Ryujinx fps does not move with guest CPU changes (emulator-bound).
- Not available: PMU counters (cycles, IPC, cache/TLB misses) on Horizon applications.

## Optimisation record template
Every optimisation gets a row here: hypothesis · target · test · before · after · regression check · conclusion.

| ID | Hypothesis | Target | Test | Before | After | Regression check | Conclusion |
|---|---|---|---|---|---|---|---|
| P1 | args_in_registers + non_argument_as_local cut main-thread time | ≥ 10 % S1 | 2 sessions/side, New Bodhum field (Save Data 01, no input), 2026-11-08 | k263 (copies off): 10.56 fps, 94.7 ms/frame, 224.6 core-ms/frame | **P1a-r1v2: 16.07 fps, 62.2 ms/frame, 149.1 core-ms/frame** | PC: r1 mirror 602 M checks 0 diff, flush-mode 0 switches, poison on; Ryujinx same route; saves unchanged | **kept: +52 % fps, -34 % total core time (work removed, not moved); vs the best old build (copies on): +17 % fps, -14 % core time** |
| P2a | ro_constants_cfg + single_floats see the fast bodies (+3,207 folds; all float ops analysed) | measurable gain | 2 pairs S1b vs P1a-r1v2 | 16.06 fps, 144.3 core-ms | 16.06 fps, 144.1 core-ms | RexRoCheck 148 M folds 0 diff; r1 616 M 0 diff; Ryujinx route | no measurable effect; kept (exact, restores coverage) |
| P2b | inline_leaves copies tiny fast leaves into calling files (16,662 calls) | measurable gain | 4 pairs S1b vs P2a-2 (A B A B, B A B A) | 16.79 fps, 136.9 core-ms | 16.75 fps, 138.1 core-ms; draws-normalised -0.57 ms +- 1.0 | PC 0 diff; Ryujinx route | no measurable effect: reverted (prototypes/P2_postpass_ports) |
| P3a | XMA Decoder on the system core 3 (banjo_system_core_xma; it preempted game threads at 0x2B on cores 0-2) | measurable gain | 12 + 8 runs S1b, toml A/B, balanced | 16.47 fps (base) | 18.06 / 18.30 fps; draws-normalised -3.5 ms +- 1.0 a frame | 0 audio underruns; saves unchanged | **kept: built-in default** (system_core 2 on top: nothing more, not set) |
| P3b | XAudio mixer thread (start 82BB9BF0, ~45 %) on core 3 too / instead of the XMA Decoder | measurable gain | 2+2+2 runs | defaults 17.6-19.6 fps | mixer+XMA 12.4-12.6 (core 3 full, console network lost); mixer only 16.9-17.3 | console put back + verified | rejected; rule: never fill core 3 |
| P3c | main thread at 0x3A, its yields to any thread | measurable gain | 4+4 runs | defaults | -0.01 ms +- 0.37 normalised | no hang, no underrun | no effect; key stays, default off |
| P4 | wait census of the main thread (diagnostic) | — | 1 run S1b | — | waits 14 % (~8.3 ms/frame) spread: effects 2.3, locks 2-2.5, job batches 1.3, audio 0.6 | — | the worker side is small |
| P4b | effect workers at 0x3A / + job workers at 0x3A | measurable gain | 8+8 / 4 runs | defaults | -0.58 +- 0.40 ms (n.s.) / game hangs | RESTORE OK, saves unchanged | not adopted / rejected |
| P5 | instruction classes of the main thread's samples (diagnostic) | - | profile prof1_p2a | - | ctx traffic 5.3 % of the thread; DEFINE bodies already localised: fast entries for them gain nothing | - | analysis |
| P5a | promoted indirect calls (360 sites, 85.5 % of calls; 80 straight into fast bodies) | exact | 4+4 runs ABBA BAAB | p4b | **-0.53 +- 0.16 ms** a frame (~1 %) | PC 181.7 M checks 0 differences, Ryujinx route, RESTORE OK, saves unchanged | **adopted** (out/builds/p5a) |
| P6-5 | dynamic resolution fixed at 612 lines (ffns_dynres_fixed_height; NOT pixel-identical) | GPU under budget | console field + battle shots | off | GPU 34.4 -> 27.9 ms in S1b; fps unchanged (CPU-bound) | normal (changing) mode broke the battle once; fixed / forced / cycling right | **default** (p6e) |
| P6-1 | FF's second shadow map every second frame (ffns_shadow_far_every = 2; NOT exact) | less CPU/GPU | 2+2 ABBA S1b | key 1 | about -1.3 ms (windows 4-10), 55 draws fewer | PC route 0 differences; shots | **default** (p6e) |

| P8-WB | guest memory window on a 2 MB boundary (ffns_window_blocks; renut-nx 1.0.2's banjo_window_blocks: virtmemFindAslr's second argument is a guard, not an alignment, so the kernel used 4 KB pages for all game memory) | exact | 2+2 ABBA S1b, p8b (wb01-wb04) | key false | **-3.44 +- 0.60 ms** a frame (draws-normalised, windows 4-13, n 33; ~6 %) | RESTORE OK, saves 0 changed | **default** (p8b) |
| P8-opt | options-menu choices, NOT exact, off unless chosen: draw distance (ffns_draw_distance), msaa_level 1, SMAA (after A2FBF0ABD9576D65), texture LOD bias | player's choice | console shots vis1 (SMAA on + MSAA off: GPU 28.2 vs 27.9 ms, render thread +2.6 ms) | - | picture right in the field (console) and the battle (PC) | - | options |
| P9-CL | generated game code + bulk copies compiled with clang 23 instead of GCC 16 (SwitchWaker's split; FFNS_CLANG_CODEGEN, tools/build/clang_launcher.py) | measurable gain | 2 x ABBA S1b (cl01-cl08) | GCC p8d 19.99/18.76 and 18.70/18.71 fps | clang exact (maytrap) 17.88/17.92: -1.5 fps; clang NOT exact (ignore) 18.80/18.55: equal | picture right (vis3_cl1); RESTORE OK, saves 0 changed | **rejected**: GCC stays; option OFF |
| P9-CB | NRO code + read-only data on 2 MB blocks (renut-nx 1.0.2 banjo_code_blocks -> ffns_code_blocks, sdk/src/ui/switch_code_blocks.cpp) | measurable gain | 2+2 ABBA S1b, p9a (cb01-cb04) | key false | **+0.81 fps (~4.3 %)**: on 20.25/19.29, off 19.39/18.53 fps | exact; RESTORE OK, saves 0 changed | **default** (p9a) |
| P9-FSR | Upscaling option (ffns_fsr 0/1/2: bilinear, + RCAS, FSR 1 EASU + RCAS) | player option | 120 s S1b, p9c (fs03-fs05) | 0: GPU 27.7-27.9 ms | 1: GPU 29.2 ms (+1.3); 2: GPU 34.1 ms (+6.2); fps unchanged (CPU-bound) | NOT exact; pictures right (vis4-vis6) | **option**, off by default |
| P10-OC | CPU clock toggle ffns_cpu_mhz (clkrst pin thread, no sys-clk; menu System > CPU clock) | user option | S1b, 120 s, windows 4-10 | 1020: ~19.7 fps | 1428 (non-PGO p10c): ~25.5 fps (+30 %) | pictures right (vis8) | **option**, off by default |
| P10-FPCR | FFNS_FPCR_FIXED + no-trapping-math (NOT exact; msr fpcr 18,352 -> 35 sites) | measurable gain | 2 + 3 runs S1b | base 20.59/19.29/20.62 | fixed 20.45/20.04 | field + battle right | **rejected** (option OFF) |
| P11-PGO | PGO (console training: field + opening battle, at 1428) + LTO | measurable gain | ABBA at 1428 + one 1020 run | 1428: ~25.5; 1020: ~19.7 | 1428: ~28.9 (battle 30.0); 1020: 25.5 | field + battle right (vis9, bt08) | **build p11** |

## 2026-11-12 / 13
Late flip, native biquad, clocks up to CPU 1785 / GPU 537 MHz and the measurements of these days:
[session-2026-11-12-13.md](session-2026-11-12-13.md). The heavy views are main-thread bound (91-93 % busy).
