# CPU performance optimization roadmap — Final Fantasy XIII-2 (ReXGlue / Renut / Switch)

> [!IMPORTANT]
> **Updated 2026-11-10 for the current build, p11** (FFXIII-2-Native-Switch tree; this repository's `main`). Section 0
> below is the current state: what became of every ranked item, the measured numbers of p11 and what is left. Sections
> 1-8 are the original research of 2026-11-07 (old project, build k250v) and are kept unchanged as the record of why
> each item was tried; their numbers are not today's.

## 0. Status of the current build (p11, 2026-11-10)

### 0.1 Where the build stands

Console, handheld, GPU 460.8 MHz and RAM 1331.2 MHz unchanged. Scene S1b (New Bodhum at night, Save Data 01, 120 s,
10-s windows 4-10), S2 (the opening battle from NEW GAME). Details: `docs/native/performance.md`,
`docs/native/30fps-steps123-2026-11-10.md`, `docs/native/progress-log.md`.

| Build | CPU clock | S1b field | Opening battle |
|---|---|---|---|
| k263 (old project's codegen, copies off) | 1020 MHz | 10.4-10.8 fps | — |
| Old project's best build (copies on, its toml) | 1020 MHz | 12.9-14.0 fps | 26.7 fps (k250v) |
| P1a-r1v2 (register-argument codegen) | 1020 MHz | 15.3-16.8 fps | — |
| p10c (all exact + non-exact defaults, no PGO) | 1020 MHz | ~19.7 fps | 27.4-30 fps |
| p10c (no PGO) | 1428 MHz | ~25.5 fps | — |
| **p11 (PGO + LTO)** | **1020 MHz** | **25.5 fps** | — |
| **p11 (PGO + LTO)** | **1428 MHz** (options menu, overclock) | **~28.9 fps** (many windows 29-30) | **30.0 fps** in every window |

At the stock clock S1b went from 10.4 fps (k263) to 25.5 fps (p11): +145 %. The GPU is at 27.9 ms a frame (612-line
render resolution, the default), under the 33.3 ms budget; the main thread is the limit.

### 0.2 What became of each ranked item (section 4)

| Rank | Item | Result | Evidence |
|---|---|---|---|
| 1 | Registers as locals, whole program | **Done (P1):** `args_in_registers` + `non_argument_as_local` (+ `instrument`, poison checks) for FF's codegen, with an exclude list of the 2,400 hooked / overridden functions. **+52 % fps, -34 % total core time** vs k263; +17 % fps vs the old project's best build. The post passes were then ported to the new "fast" bodies (P2a: exact, no measurable effect; P2b inline leaves: no effect, reverted). | console A/B 2026-11-08, PC r1 mirror 602 M checks 0 differences |
| 2 | FPCR flush-mode switches | **Tested, rejected (P10-FPCR):** a fixed flush mode with `-fno-trapping-math` cut the `msr fpcr` sites in the ELF from 18,352 to 35, and fps did not move (20.45 / 20.04 vs 20.59 / 19.29 / 20.62). The switches are not a bottleneck on this code. Option `FFNS_FPCR_FIXED` stays, OFF. | 2 + 3 runs S1b |
| 3 | Main-thread waits and scheduling | **Measured and acted on (P3, P4):** the XMA decoder on core 3 is **+6 %** (-3.5 ms a frame) and is a built-in default; the XAudio mixer on core 3 starved the console (rejected, rule: never fill core 3); main thread at 0x3A: no effect; wait census: the main thread waits 14 % (~8.3 ms a frame) spread over effects, locks, job batches and audio; effect / job workers raised: not significant or a hang (rejected). | 12 + 8 + 6 + 8 + 16 runs |
| 4 | Copy boundary pruning | **Superseded:** with P1's codegen the bulk machine copies were retired (`banjo_cpu_native_bulk = false` by default). Copies × the register diet × funclets crashed at boot (round-2 copy of `sub_82B67CF0`), and P1 with copies off beat the old build with copies on. | bisect 2026-11-08 |
| 5 | Shader variant cache | **Done for the package:** the build makes the DKSH cache from the recorded pipelines of five play-throughs + every shader's generic variant (51,921 variants); the complete GLSL package from a scan of the game files (no capture runs). Not re-measured as a CPU item. | `tools/gh/build_switch.ps1` steps shaders / cache |
| 6 | CaptureDelta dirty tracking | **Open.** The guest inclusive profile (P6) puts Renut's render job at 30.5 % of the main thread (draw submission ~17 ms a frame): still the biggest single block. Overlapping it with the next frame's update (P7) was investigated with an exact race probe and is **not safe** (~4,480 + ~3,820 shared 4-byte granules a frame). | p6d profile, P7 probe |
| 7 | Native XAudio mixer | **Open.** The mixer thread was identified (start `82BB9BF0`, ~45 % of a core); moving it was rejected (rank 3); no native mixer yet. | P3b |
| 8 | PGO retraining / build A/Bs | **Done (P11):** PGO trained on the console at 1428 MHz (New Bodhum field + the opening battle), LTO on: **+29 % at 1020 MHz** (19.7 → 25.5 fps), +13 % at 1428 MHz. The profile is `pgo/` (263 .gcda, GCC 16.1). clang 23 for the generated code instead of GCC was also tried (P9-CL): exact -1.5 fps, inexact equal → rejected. `-fipa-pta`: not tried yet. | ABBA runs, field + battle pictures checked |
| 9 | More copies / hand natives | **Replaced by exact codegen-wide work:** promoted indirect calls (P5a: 360 sites = 85.5 % of the calls, 80 straight into fast bodies) **-0.53 ms a frame** (~1 %), adopted. Instruction classes of the main thread (P5): ALU 37 %, guest memory ~24 %, branches 9 %, byte swaps 8 %, host stack 8 %, PPCContext 7.8 %; the top 50 functions are only 20 % of it (flat). The one FF hand native (`sub_8228EFC0`, 4x4 matrix multiply) has not been re-measured since P1. | `docs/optimising-functions.md` |
| 10-13 | ObjectTable, ABI checks, memmove callers, effect-worker spins | Open (each about 1 % or less). `args_in_registers` is on for FF (rank 11). | — |
| 14 | TLB / large pages | **Done, two exact wins (renut-nx 1.0.2):** the guest memory window on a 2 MB boundary (`ffns_window_blocks`: the kernel had used 4 KB pages for all game memory) **-3.44 ms a frame (~6 %)**; the NRO's code and read-only data on 2 MB blocks (`ffns_code_blocks`) **+4.3 %**. Both default on. | ABBA S1b p8b, p9a |
| 15 | Toolchain flags | Checked: `-march=armv8-a+crc+crypto -mtune=cortex-a57` (`tools/switch/cmake/switch-devkitA64.cmake`), devkitA64 GCC 16.1. | — |

Beyond the list (not exact, the user's choices): the render resolution fixed at 612 lines (GPU 34.4 → 27.9 ms), the far
shadow map every second frame (about -1.3 ms), and the **CPU clock toggle** (options menu, 1224 / 1428 MHz, set by the
game through clkrst without sys-clk: +30 % at 1428; off by default; see the README's overclock warning).

### 0.3 What is left for 30 fps at the stock clock

S1b at 1020 MHz is 25.5 fps (~39 ms a frame); 30 fps needs about 6 ms less on the main thread. The job census (P6)
shows 44 ms of jobs a frame on the main thread, 34 ms of them with the three job workers idle. In order:

1. **Renut's draw submission on the main thread** (render job, ~17 ms a frame): CaptureDelta dirty tracking (rank 6)
   and cheaper per-draw state work — exact.
2. **PGO retraining after every source change** (a changed function loses its counts), with more scenes (menus,
   cutscenes, more battles); A/B `-fipa-pta`.
3. **The audio side:** a native XAudio mixer (rank 7) frees ~45 % of a shared core.
4. Smaller exact items (ranks 10-13), each about 1 %.

---

## Original report (2026-11-07, old project, build k250v)

Research-only session, 2026-11-07 (work order "2-Hour Autonomous CPU & Performance Optimization Research").
No source, build, shader, renderer or settings change was made for this report. Builds and runs cited were made
earlier the same day for the 30 fps work (`docs/diagnostics/PERF_30FPS.md`).

**Evidence labels used below:** MEASURED (console log or profile named), CODE (read in the source), INFERRED (follows
from code or measurements but not checked directly), EXTERNAL (documentation / other projects), MISSING (needed, not
available).

**Sources of evidence used:**

| Item | What it is |
|---|---|
| k250v | Console run, 2026-11-07 21:34. The build the user has (PGO + LTO, 541 native copies, ABI sync, hand matmul, compile core 3, dynamic resolution). 300 s scripted battle. `rex_profile.log`: per-thread CPU every 10 s, no stack sampling. |
| k247s | Console run, 17:57. PGO build with the 1 ms stack sampler (`switch_profile_stacks`). Symbolized with `tools/switch_hot_symbols.py` into `out/k247s_hot_main.txt`, `_all.txt`, `_x.txt`. |
| k246b | Console run, 15:27. Everyday build with the stack sampler (`out/k246b_hot_main150.txt`). |
| Source | `/home/claude/src` (the cloud copy of app + sdk, 2026-11-03..07) and files staged from the PC before it went off: `app/generated/hot/n_829FFA58.h`, `app/CMakeLists.txt`, `queued.cpp`, `docs/*`. |

---

## 1. Executive summary

**Situation.** At the official handheld clocks (CPU 1020 MHz, GPU 460.8 MHz, EMC 1331.2 MHz) the shipped build
averages 26.7 fps in the scripted battle. It holds 29.4–30 fps in the lighter second half and drops to 19–25 fps in the
heavy phases. Dynamic resolution keeps the GPU at 24–28 ms a frame, so the GPU is no longer the limit. The game's
**Main Thread** is.

**Why "Core 2 is at 99 %".** MEASURED (k250v, 10-s blocks 14–27, the heavy phase):
- The main thread uses 83–89 % of a core. That is **30–45 ms of CPU per frame** for one thread whose frame must fit
  33.3 ms.
- In the 30-fps blocks (34–45) it needs only 17–20 ms a frame.
- Every game thread has mask 0x7 (cores 0–2), preferred core −1 and priority 0x3B. The main thread is therefore not
  pinned. The overlay's busiest core is simply the core the main thread has settled on, plus the other 0x3B threads
  that land there.
- The other big consumers on cores 0–2 in battle:
  - the audio mixer XThread: 30–37 % of a core;
  - the unnamed "?" threads (the busiest is the render side, pinned to core 1 with mask 0x2): 20–44 %;
  - the XMA decoder: 13–18 %;
  - four vfx-qix effect workers: 4–24 % together.
- Core 3 stays about 50 % idle.

**What is wasting the most CPU on the main thread.** MEASURED (k247s, 3,874 samples):
- 50 % is recompiled game code without a native copy.
- 33 % is the native copies.
- 15 % is SDK / app code, including Renut's per-draw device snapshot `CaptureDelta` at 2.8 %.
- 2 % is the C library.

So about **83 % of the critical thread is the recompiled PowerPC code itself**. The biggest realistic gains are in how
that code is generated:

1. **Non-volatile and non-argument registers as C++ locals in the whole generated program** (XenonRecomp's
   "largest gains" option). INFERRED to be off for FF: the hottest copy still calls `__savegprlr_20` /
   `__restgprlr_20`, which a localized function never does (`builder_context.h`, `keepsNonVolatilesInCtx`).
2. **FPCR flush-mode switches** between VMX and scalar FP. Each write costs 40–43 ns, i.e. 41–44 cycles, MEASURED on
   this console. The hottest function alone has 18 unconditional and 60 conditional switch sites.
3. **Register traffic at the boundaries of the native copies.** The top copy loads 81 context registers on entry and
   writes 81 back at every return. The Cortex-A57 has a 12-entry store queue, and no memory disambiguation, so loads
   wait for older store addresses.
4. **Measuring the main thread's waits and scheduling** at priority 0x3B on shared cores. This is a hypothesis to
   measure first, not a fix.

**Expected overall impact.** No precise FPS number can be promised without measurements. The target is clear,
though: the heavy phase needs **about 20–30 % less main-thread CPU per frame** (from about 38 ms to under about 30 ms).
Items 1–3 attack the 83 % share that is recompiled code. Item 1 on its own was reported to cut "several milliseconds"
of frame time in Unleashed Recompiled (EXTERNAL). Every number in this report beyond the measured ones is a hypothesis
to be confirmed with the profiling plan (section 7).

**One conflict with this work order.** The work order forbids lowering resolution. Dynamic resolution (720/612/540
lines) was explicitly allowed by the user earlier on 2026-11-07 and is on in the shipped `ffxiii2_30fps.toml`. Nothing
in this roadmap adds a visual change. Whether dynamic resolution stays on is the user's call.

---

## 2. Existing optimization inventory

Status vocabulary:
- Implemented
- Partial
- Tested & rejected
- Investigated, unresolved
- Proposed (not implemented)
- Exists, not used for FF

| Optimization | Status | Previous investigation / evidence | Remaining potential | More work? |
|---|---|---|---|---|
| Static recompilation (rexglue codegen + `tools/post_codegen.py` passes: inline leaves, inline memcalls, store cards, spin polls, flush-flow) | Implemented | Since 2026-11-02 | See section 4 (codegen options) | Yes |
| Codegen: ctr / xer / reserved / cr as locals, skip_msr (CLAUDE.md 3.4) | Implemented | | | No |
| Codegen: **non_volatile_as_local / non_argument_as_local** | **Exists, not used for FF (INFERRED)** | SDK default `false` (`sdk/include/rex/codegen/config.h:192`); copies still call `__savegprlr_N` | VERY HIGH | Yes, rank 1 |
| Codegen: args_in_registers (`sdk/src/codegen/args_in_registers.cpp`) | Exists; FF status UNKNOWN | | HIGH if off | Check |
| Codegen: fp_moves_modeless (`diet_ops.h`; SA2 spent 20–26 % after FPCR writes) | Exists; FF status UNKNOWN | | HIGH if off | Check |
| "fp mode lookahead" + `BANJO_SWITCH_FLUSH_FLOW` (checks proven by control flow compile away) | Implemented (log: "optimised codegen (fp mode lookahead, CPU rounds 1-3)") | | Partial: 78 sites remain in the top function | Yes, rank 3 |
| Native copies of hot functions (`localized_bulk.txt`, sa2-nx / bejeweled3-nx) | Implemented: 541 copies (k248) | 211 → 547 entries; k250v without crash | Coverage + boundary cost | Yes, ranks 4, 9 |
| ABI sync in copies (r14–r31 / f14–f31 kept across unknown calls) | Implemented (k249) + fixed (k250) | k249 crash: save/restore helpers; fixed with `ABI_HELPER` | Done | No |
| Hand native: 4×4 matrix multiply `sub_8228EFC0` | Implemented (k249) | 1.4–1.5 % of the main thread before | Verify its share now | Measure |
| PGO + LTO (sa2-nx / banjo-nx machinery) | Implemented (k250): 24.0 → 26.7 fps | One training battle | More training scenes; `-fipa-pta` A/B | Yes, rank 8 |
| `-fipa-pta` (`BANJO_SWITCH_IPA_PTA`) | Exists, off, untested | sa2-nx and UnleashedRecomp-NX use it (CMakeLists comment) | UNKNOWN | A/B |
| Inline atomics (`BANJO_SWITCH_OUTLINE_ATOMICS` OFF), omit frame pointer, guest CAS without dmb, NEON VMX helpers, NEON shuffles, vector byte swap | Implemented | | | No |
| GCC early-RA off (miscompiles) | Implemented (safety) | Peggle 2 miscompile | None | No |
| Spin backoff for 22 polling loops (`ffxiii2_spin_backoff`) | Implemented | 34 vs 7.7 fps on the console | Effect workers still 4–24 % | Low |
| Guest critical sections with an atomic fast path (`RtlEnterCriticalSection_raw`) | Implemented (CODE) | | Small | No |
| ObjectTable lookups under a global recursive mutex | Implemented as in the SDK | 3,700–3,900 lookups/s, 3–4 % contended (k250v) | Low | Rank 10 |
| Store cards instead of hashing streams / textures | Implemented (`banjo_renut_store_cards` on) | | One `strb` per guest store; still cheaper than hashing | No |
| Renut device snapshot deltas + NEON line compare (`sa2_renut_snapshot_deltas`) | Implemented | k205: 0 differ in 2.5 M draws | `CaptureDelta` still 2.8 % | Rank 6 |
| Dirty-bit device capture (compare only marked chunks) | Proposed (PERF_30FPS item 12) | | MEDIUM | Rank 6 |
| Dynamic resolution (GPU) | Implemented, user-approved visual change | GPU 39 → 28 ms | None for the CPU | No |
| Shader work on core 3 (`banjo_background_compile_core = 3`) | Implemented | Moves compiles off the game's cores | | No (it moves work, see rank 5) |
| DKSH cache (rc9, 505 MB) | Partial | Battle variants still compiled at run time; the uam thread was 22 % of all samples in k247s | MEDIUM | Rank 5 |
| Async shader variants, compile wait boost | Implemented | | | No |
| Four-core layout (`banjo_system_core=2`, XMA on core 3) | Tested & rejected for now | +0.4 fps; one block with 59 audio requests without data (k247c) | Revisit after the work is reduced | Later |
| Fused vmaddfp (k236) | Tested & rejected | No reproducible effect; reverted | None | No |
| Native XAudio SRC / reverb (`banjo_native_xaudio_src`, `banjo_native_reverb`) | Exists, not used for FF (addresses are another game's) | FF's mixer: sub_82BB4008 27 % + sub_82A34BB0 25 % of the audio thread | MEDIUM (cores 0–2 load) | Rank 7 |
| banjo direct state / native D3D draws / API clear / resolve | Exists, off for FF (log) | banjo-nx's PM4 path, not Renut's | UNKNOWN, mostly not applicable to Renut | No |
| 39-bit guest window (one 0x120000000 reservation, base + zero-extended address) | Implemented | PERF_30FPS | None | No |
| D-003 band, D-006 stream attach, shadow atlas, depth-bind lock, split-screen identity (k229) | Rendering investigations (unresolved / suspected / fixed) | `docs/diagnostics/BUGS.md`, `claude/RENDERER_ARCHITECTURE.md` | Not CPU items; not repeated here. Any renderer change below must keep the k229 origin identity and D-007 / D-008 copies. | — |

---

## 3. Core 2 bottleneck analysis

### 3.1 What runs there

MEASURED (k250v `rex_profile.log`). Per-thread CPU every 10 s; values are % of one core:

| Thread | Mask / priority | Battle, heavy phase | Battle, 30 fps phase |
|---|---|---|---|
| Main Thread (F8…) | 0x7 / 0x3B | 83–89 % = 30–45 ms per frame | 51–60 % = 17–20 ms per frame |
| XThread1AB909A0 (audio mixer, see k247s) | 0x7 / 0x3B | 27–36 % | 30–34 % |
| "?" (Renut render side, mask 0x2) | 0x2 / 0x2C | 24–31 % | 20–44 % |
| XMA Decoder | 0x7 / 0x2B | 11–18 % | 12–17 % |
| vfx-qix-effect × 4 | 0x7 / 0x3B | 11–24 % together | 4–11 % |
| XThreadC14EB00 and smaller game threads | 0x7 / 0x3B | ~10–15 % | ~10 % |

- Total process CPU in battle: 140–200 % of 400 %.
- "System core 3: idle 50 %" in every battle block (`banjo_system_core 1; game threads on 0x7`).

The answer to questions 1–2 of the work order is therefore:
- Core 2's 99 % is the **game's Main Thread** (the guest thread that runs FF's frame), together with whatever other
  0x3B thread shares that core at the moment.
- It is not the recompiler, which is static and not running. It is not the shader compiler, which is now on core 3.
  It is not the render thread, which is pinned to core 1.

### 3.2 What the main thread spends its time on

MEASURED (k247s, PGO build, 3,874 main-thread samples; k246b similar):

| Category | Share |
|---|---|
| Recompiled game code without a copy (thousands of functions, flat profile) | 49.9 % |
| Native copies (`banjo::hot::n_*`) | 33.0 % |
| SDK and app (Renut D3D layer, kernel exports) | 14.9 % |
| C library (memmove 1.1 %, malloc) | 2.2 % |

The top functions are a flat list:

| Function | Share |
|---|---|
| sub_829FFA58 (copy) | 5.3 % |
| sub_829F14D8 | 4.1 % |
| sub_828AABF0 | 3.1 % |
| **CaptureDelta** | **2.8 %** |
| sub_82590988 | 2.0 % |
| sub_8228EFC0 (matrix multiply, now a hand native) | 1.5 % |
| sub_8251B768 | 1.4 % |
| memmove | 1.1 % |
| __imp__sub_82972D48 (FF's pass-1 list) | 1.0 % |
| mutexLock + mutexUnlock + pthread_mutex_lock | ≈1.9 % |
| ObjectTable::LookupObject | 0.7 % |
| Ffxiii2StableSurface | 0.7 % |
| CheckTitleTermination | 0.5 % |

Renut work on the game thread is CaptureDelta, QueueCapture, SetStreamSource, StableSurface, the Ss*MipLevel
functions and FlushDeferredVertices: about 5 % in total.

**Conclusion.** The critical path is dominated by the generated code's execution cost, spread over hundreds of
functions. Hand natives can only pick off the top few percent each. Code-generation-wide improvements apply to the
whole 83 %.

### 3.3 Potential causes, mapped to the work order's questions

| Question | Answer and evidence |
|---|---|
| 3. Functions that run unnecessarily often? | MISSING: no call counts per frame on the console. The Counting policy (`f_*<Counting>`) and `tools/hot_arm_compare.py` can count copy block entries on the PC. |
| 4. Repeated calculations that could be cached? | CaptureDelta compares the 13,088-byte device snapshot per draw: 36–142 k draws per 10 s in battle (MEASURED) ≈ 150–540 draws a frame ≈ 2–7 MB of compares a frame. |
| 5. Redundant state updates? | (a) The copies write back all 81 context registers at each return (CODE: n_829FFA58.h header); (b) FPCR mode writes on VMX ↔ FPU transitions (CODE: 78 sites in the top function). |
| 6. Excessive synchronization? | ObjectTable 3,700–3,900 lookups/s under one recursive mutex, 3–4 % contended (MEASURED); mutex functions ≈1.9 % of the main thread. Moderate, not dominant. |
| 7. CPU-bound or waiting? | In the heavy phase the main thread runs 83–89 % of wall time (MEASURED). k247s's sampler reported "25 % waiting in the kernel" for the main thread in a 28.7 fps battle block (a lighter phase). MISSING: what it waits on (GPU fence, render-thread queue, effect workers, audio) per frame. |
| 8. Scheduler-caused? | Plausible, unmeasured. All busy game threads run at priority 0x3B on cores 0–2. Horizon rotates the run queue of threads at a *preemption priority* every 10 ms (EXTERNAL: yuzu's `GlobalSchedulerContext::PreemptThreads`, "happens every 10ms"). In yuzu / Ryujinx that priority is 59 = 0x3B on cores 0–2 (recalled from their sources; verify). If the main thread shares a core with another runnable 0x3B thread, it can lose whole 10 ms slices. MISSING: per-frame run time vs wall time of the main thread. |
| 9. Windows scheduler? | Only relevant to PC observations. The Switch shows the same picture: the main thread is 83–89 % busy on its own. |
| 10. Same on the Switch? | Yes. The data above is from the console at 1020 MHz. |

### 3.4 How to identify the responsible thread and functions (tools)

The most practical tool is the one already built into the NRO:
- The rex profiler prints per-thread CPU % every 10 s and runs a 1 ms stack sampler with `switch_profile_stacks`.
- `tools/switch_hot_symbols.py` symbolizes the samples with the exact ELF.
- `tools/switch_hot_bulk.py` turns them into copy lists.

Its overhead is low: 1 sample per ms. It runs on the real console at the real clock, which is what decides
performance. Gaps to close, all small additions to the existing profiler:
- per-frame main-thread CPU and wait time, with the wait object;
- "ready but not running" time;
- copy hit counts;
- the FPCR write census on the console (it is compiled out there today: `sys_counters.h` defines
  `REX_SYS_CENSUS` to nothing on `__SWITCH__`).

On the PC, use these only as proxies:
- Windows Performance Recorder / Analyzer: CPU Usage (Precise) shows ready time and the waking thread per context
  switch. It is free and already part of Windows.
- The PC census counters (`REX_SYS_CENSUS`): FPCR writes per frame are a property of the guest code path, so the PC
  count × 43 ns estimates the Switch cost.

Not recommended now:
- Tracy, Superluminal and VTune need downloads, which the standing rules forbid without approval. They also measure
  x86, not the A57.
- Linux perf does not exist on Horizon.
- The A57 PMU counters (TLB misses, store-queue stalls) are not exposed to applications as far as is known. MISSING:
  confirm whether an svc or a sysmodule can read them.

### 3.5 Evidence still missing (in priority order)

1. A per-frame main-thread breakdown in the heavy phase: run time, ready time, wait time by object.
2. The stack-sampled profile of the **k250v** build, the one the user has. The k247s profile predates the 336 new
   copies, the ABI sync and the hand natives.
3. FPCR writes per frame (PC census) and the share of samples right after an `msr fpcr` (console).
4. Confirmation of FF's codegen TOML flags: non_volatile_as_local, non_argument_as_local, args_in_registers,
   fp_moves_modeless.
5. Call counts per frame of the top 50 functions (PC Counting policy).

---

## 4. Ranked optimization opportunities

Sorted by expected improvement on the main thread at 1020 MHz. "Switch relevance" means how much the effect depends
on the A57 specifically.

| Rank | Optimization | Subsystem | Expected CPU improvement | Switch relevance | Difficulty | Risk | Evidence |
|---|---|---|---|---|---|---|---|
| 1 | Whole-program **non_volatile_as_local + non_argument_as_local** for FF's codegen (with an exclude list) | Recompiler codegen | VERY HIGH | Very high (store-queue / disambiguation limits) | Medium (config + verification) | Medium–high (ABI violations, SEH funclets, hand asm) | INFERRED off; EXTERNAL XenonRecomp "largest gains"; CODE support present |
| 2 | **FPCR flush-mode switches**: count, then remove by wider lookahead, mode contracts at calls, software flush for isolated VMX ops | Recompiler codegen / SDK | HIGH (if the census confirms frequent writes) | Very high (41–44 cycles a write on this A57; x86 MXCSR is cheaper) | Medium | Medium (denormal semantics) | MEASURED cost; CODE 78 sites in the top function |
| 3 | **Main-thread wait and scheduling analysis**, then (with approval) fix what it shows, e.g. the main thread at a priority above 0x3B | Runtime / threading | HIGH or none (unknown until measured) | Horizon-specific (10 ms preemption rotation) | Low to measure | Low to measure; a priority change needs the user | INFERRED + EXTERNAL; MISSING data |
| 4 | **Copy boundary pruning**: load only live-in registers and write back only registers the copy may have changed; skip vector registers the copy never writes | Native copies (`tools/localize_native.py`) | HIGH (applies to the 33 % in copies) | Very high (12-entry store queue, no disambiguation, 128-entry register file with 4 entries per 128-bit register) | Low–medium | Low (the guard checks it) | CODE n_829FFA58.h: 81 in / 81 out; EXTERNAL A57 microarchitecture |
| 5 | **Shader variant cache completeness** for battles, field and cutscenes (no uam compiles in play) | Renderer / shaders | MEDIUM (indirect: frees shared cores; removes compile stalls) | High | Low | Low | MEASURED: uam thread 22 % of all samples (k247s) |
| 6 | **CaptureDelta** with dirty tracking from FF's D3D setters (known hook addresses) instead of comparing the whole device per draw | Renut game-thread side | MEDIUM (≤2.8 % of the main thread + memory traffic) | High (L1 32 KB, about 8 GB/s measured DRAM bandwidth) | Medium–high | Medium (must keep 0-differ; verify mode exists) | MEASURED 2.8 %; CODE queued.cpp |
| 7 | **Native XAudio mixer for FF** (sub_82BB4008, sub_82A34BB0: 52 % of the audio thread), porting banjo_native_xaudio_src if FF's XDK code matches | Audio | MEDIUM (cores 0–2: −10–15 % of a core) | High | Medium | Low–medium (bit-exact verify mode exists) | MEASURED (k247s _x) |
| 8 | **PGO retraining** with field + menu + cutscene + several battles; A/B `-fipa-pta`, inliner limits | Build | MEDIUM | High | Low | Low | MEASURED: one session gave +11 % (k250) |
| 9 | **More copies / hand natives** for the next 30 uncopied hot functions after a fresh k250v profile | Native copies | MEDIUM | High | Low per function | Low (guard) | MEASURED: 50 % still uncopied (k247s) |
| 10 | ObjectTable: lock-free read of handle slots (or per-thread cache); CheckTitleTermination flag test first | SDK runtime | LOW (≈1–2 %) | Medium | Low | Low–medium | MEASURED 3.7–3.9 k lookups/s |
| 11 | args_in_registers / fp_moves_modeless: check they are on for FF | Codegen | UNKNOWN (HIGH if off) | High | Low (check) | Medium | CODE (exist in SDK) |
| 12 | memmove / malloc callers on the main thread (1.3 %) | Game code / C library | LOW | Medium | Low | Low | MEASURED |
| 13 | Effect-worker spin reduction (vfx-qix: up to 24 % of a core) | Runtime (spin polls) | LOW–MEDIUM (shared cores) | Medium | Low | Low | MEASURED |
| 14 | TLB / large pages for the guest heap | Memory | UNKNOWN | Possibly high (32-entry L1 DTLB, 4 KB pages: 128 KB reach) | High | Medium | EXTERNAL; MISSING PMU data |
| 15 | Toolchain flags: confirm `-mcpu/-mtune=cortex-a57` in `tools/switch/cmake/switch-devkitA64.cmake` | Build | LOW–UNKNOWN | High | Trivial (check) | Low | MISSING |

### Reasoning behind the ranking

**1 — registers as locals.**
- What it removes: without it, every r14–r31 / f14–f31 / v64+ access and every non-argument register (r0, r2, r11,
  r12, f0, v32–v63) is a field of `PPCContext`. GCC must store dirty fields before every call and reload them after.
  FF's code is call-dense C++ (17–24 callees in the hot functions).
- It also removes every `__savegprlr_N` / `__restgprlr_N` / `__savefpr_N` / `__restfpr_N` call: 18-register stores
  and loads to the guest stack, through big-endian swaps.
- On the A57 these stores are expensive. The store queue has only 12 entries. Loads cannot pass older stores whose
  address is unknown. Store-to-load forwarding costs about 7 cycles (EXTERNAL: Chips and Cheese).
- It applies to the 50 % uncopied share, and makes the copies cheaper as well.
- XenonRecomp's documentation calls it the optimization with "the largest gains"; Unleashed Recompiled reports
  "frame times drop by several milliseconds" (EXTERNAL).
- Why VERY HIGH: the effect is broad and the mechanism is well understood.

**2 — FPCR switches.**
- MEASURED on this console: a write costs 40.2 ns, or 43.1 ns when it toggles the flush mode (≈41–44 cycles at
  1020 MHz).
- In n_829FFA58.h, a scalar `fmuls` + `stfs` is followed by a VMX multiply and needs
  `enableFlushModeUnconditional()`. The same function has 14 unconditional enables, 4 unconditional disables and 60
  conditional ones.
- sa2-nx found its VMX-heavy game thread spending 20–26 % of its time right after such writes (CODE comment in
  `diet_ops.h`).
- It ranks below 1 only because FF's census is not measured yet.

**3 — waits and scheduling.**
- The heavy phase shows 83–89 % busy, so the main thread does not wait much there. Its per-frame CPU (30–45 ms) is
  already above budget, so scheduling alone cannot reach 30 fps.
- The 10 ms preemption rotation at 0x3B could still explain part of the variance between blocks of equal work.
- It is ranked high because measuring it is cheap and its potential is large if confirmed. A priority change is a
  threading change and needs the user's approval.

**4 — copy boundaries.**
- The copies already remove most context traffic inside a function. Their entry and exit still move up to 81
  registers, 40 of them 128-bit vectors (640 bytes each way).
- On the A57, 128-bit values take 4 entries of the 128-entry register file, so 40 vector locals cannot all live in
  registers. The compiler spills, and the exit stores flood the 12-entry store queue.
- The fix is in the generator (`localize_native.py` already computes `written` and `regs` sets) and the existing guard
  verifies it.

**5 — shader cache.**
- The thread that runs uam took 22 % of all samples in k247s, 27 % of that in `nv50_ir`. It now runs on core 3, so it
  no longer competes with the main thread directly. That move does not remove the work.
- A complete cache does remove it, along with the first-use stalls.

**6 — CaptureDelta.**
- 2.8 % of the main thread is measured, but compare bandwidth also evicts the game's L1 and L2 data: 13 KB per draw
  against a 32 KB L1.
- Draw counts per frame are high (150–540).

**7 — XAudio mixer.**
- Not on the main thread, but it is the second-busiest thread on cores 0–2 (27–37 %).
- Removing its work lowers the chance that the main thread shares a core with a runnable 0x3B thread.

**8–15** are smaller or need evidence first.

---

## 5. Highest-impact candidates in detail

### 5.1 Non-volatile / non-argument registers as locals, whole program (rank 1)

1. **Problem.**
   - FF's generated code keeps r14–r31, f14–f31, v64–v127 and the scratch registers in `PPCContext` (INFERRED).
   - Functions call the guest's `__savegprlr_N` / `__restgprlr_N` helpers, which copy registers between the context
     and the guest stack. Every call boundary forces context stores and reloads.
2. **Why it costs CPU.**
   - Memory traffic instead of register moves: stores, big-endian swaps, stack writes.
   - Serialized dependency chains through store-to-load forwarding (≈7 cycles on the A57).
   - Loads blocked behind stores with unresolved addresses (no memory disambiguation on the A57).
3. **Solution.**
   - Set `non_volatile_as_local = true` and `non_argument_as_local = true` in FF's codegen TOML.
   - Fill `non_volatile_as_local_exclude` for functions that break the ABI (hand-written assembly libraries, setjmp /
     longjmp users, SEH funclets; the SDK already keeps funclets and save/restore helpers in ctx), and
     `non_argument_as_local_keep` for callees that take an argument in r12 / r11 (the stack probe).
   - Then regenerate. The native copies' own ABI-sync logic becomes partly redundant.
4. **Files and subsystems.**
   - FF's codegen TOML (in `app/`; not in the cloud copy).
   - `sdk/src/codegen/builders/context.cpp`, `builder_context.h`, `control_flow.cpp`, `config.cpp`.
   - `tools/post_codegen.py` passes that assume ctx registers.
   - `tools/localize_native.py`.
   - Hooks that read non-volatiles from ctx (audit all `REX_FUNC` hooks for ctx.r14–r31 reads).
5. **Core 2 (main thread).** Broad: every generated function on the main thread. VERY HIGH, but not quantified.
6. **Switch.** Larger than on the PC: the A57 is far more sensitive to store traffic than a desktop core (a 12-entry
   store queue, several times smaller than current desktop cores', and no memory disambiguation).
7. **Difficulty.** Medium: the SDK supports it and other ports (katamari-nx, since 2026-10-30) use it. The work is the
   exclude list and the verification.
8. **Possible regressions.**
   - Wrong values in callers when a callee violates the ABI (the k249 crash was this class).
   - Hooks that read callee-saved registers from ctx.
   - Exceptions / longjmp.
9. **Benchmark.**
   - PC guard run (`banjo_cpu_verify`) and PC battle comparison.
   - Then console A/B, 300 s each: k250v vs the new build.
   - Metrics: main-thread ms per frame in the heavy phase, battle fps, total core-ms per frame.
10. **Profiling first?** Only to confirm the current flag (rexglue logs "Codegen config: … non_argument=… non_volatile=…"
    at debug level in `runtime.cpp`; or read the TOML).

### 5.2 FPCR flush-mode switches (rank 2)

1. **Problem.**
   - The 360's VMX flushes denormals and its FPU does not. The A57 has one FPCR.FZ bit for both, so the generated code
     writes FPCR at every VMX ↔ scalar-FP boundary that its lookahead cannot prove redundant.
   - Example from n_829FFA58.h: `stfs` of an `fmuls` result, then `enableFlushModeUnconditional()`, then two vector
     multiplies.
2. **Why it costs CPU.** `msr fpcr` costs 41–44 cycles (MEASURED at startup, "FPCR on this CPU" line), and later FP
   instructions depend on it.
3. **Solution, in order of safety.**
   - (a) Count first: the PC census (`kCensusFlushWrite`, `kCensusFlushSame`) per frame in the battle.
   - (b) Extend the lookahead across direct calls with known mode effects (a per-function "mode on entry / on exit"
     summary, like the reach analysis already in `localize_native.py`).
   - (c) Use the fp_moves_modeless helpers wherever scalar FP is only moved, not computed.
   - (d) For an isolated VMX op inside scalar code: run it in IEEE mode and flush denormal inputs and outputs with NEON
     bit operations (compare the exponent field to zero, `bsl` with signed zero), 3–4 instructions instead of two FPCR
     writes. This must be bit-exact with the 360's VMX for NaN and zero signs (the k238 vmaxfp lesson).
4. **Files.**
   - `sdk/include/rex/ppc/context.h` (FPSCRRegister).
   - `diet_ops.h`.
   - The codegen's fp-mode lookahead (`sdk/src/codegen/builders/`).
   - `tools/flush_mode_flow.py`.
5. **Core 2.** HIGH if writes are in the tens of thousands per frame. That is unknown until counted.
6. **Switch.** Specific to the A57: x86 MXCSR writes are cheaper and PCs hide them.
7. **Difficulty.** Medium.
8. **Regressions.** Denormal results differ: rare, but they can change game logic. Needs the PC verify guard and a
   pixel comparison.
9. **Benchmark.** Census writes per frame before and after (PC); console A/B as in 5.1.
10. **Profiling first?** Yes: the census.

### 5.3 Copy boundary pruning (rank 4)

1. **Problem.**
   - A copy loads every context register it names on entry (81 in sub_829FFA58, 40 of them vectors) and stores all 81
     back at each return. The header says "81 of them written back at a return".
   - The guard needs exact context state at returns, but only for registers the copy *may have changed*. Writing back
     an unchanged register stores its entry value again.
2. **Why it costs CPU.**
   - Up to 81 stores per return: 12-entry store queue, one store per cycle.
   - Register pressure from 40 vector locals on a 128-entry register file where each 128-bit value takes 4 entries.
3. **Solution.**
   - In `tools/localize_native.py`, emit `ctx.X = l_X` at a return only for X in the function's `written` set, which
     already exists (`dirty_sets` / `written_registers`).
   - Load on entry only the registers read before written on some path (live-in).
   - Keep callee-saved semantics (ABI sync) as now.
4. **Files.** `tools/localize_native.py`; `app/generated/hot/*` (regenerated).
5. **Core 2.** Part of the 33 % in copies. The hottest copies are called often from the battle loop.
6. **Switch.** High (store queue).
7. **Difficulty.** Low–medium.
8. **Regressions.** A missed write-back. The guard (`banjo_cpu_verify`) compares every register the original writes,
   so it catches this on the PC.
9. **Benchmark.** Copy hit counts × instructions (`tools/hot_arm_compare.py`); console A/B.
10. **Profiling first?** No.

### 5.4 Main-thread wait and scheduling measurement (rank 3)

1. **Problem.**
   - Unknown waits on the critical thread.
   - All busy threads at 0x3B share cores 0–2 while core 3 is half idle.
   - Horizon rotates preemption-priority run queues every 10 ms (EXTERNAL; the exact priorities need confirming).
2. **Why it costs.** A 10 ms slice lost to another thread is 30 % of the frame budget.
3. **Solution.**
   - Measure first: per-frame main-thread run time (svcGetInfo thread tick count) vs wall time, and the wait objects
     with durations (the SDK's `XObject::Wait` / `WaitMultiple` paths already count "parked").
   - If contention is shown, and only with the user's approval: raise the main thread above 0x3B (it is the critical
     path), or keep the audio and effect threads off its core. That is a placement rule; no work moves.
4. **Files.**
   - `sdk/src/system/xthread.cpp` (thread creation / priority mapping).
   - The profiler (`app/src/profiler/`).
5. **Core 2.** Fewer lost slices; more stable frame times.
6. **Switch.** Horizon-specific.
7. **Difficulty.** Low to measure.
8. **Regressions.** Priority inversion: the game's own locks wait on lower-priority threads. Audio underruns (the
   k247c lesson).
9. **Benchmark.** Main-thread ready time per frame → 0; frame-time variance.
10. **Profiling first?** Yes (that is the item).

### 5.5 CaptureDelta dirty tracking (rank 6)

1. **Problem.** Each draw compares the whole guest D3D device snapshot (13,088 bytes; narrow list of read chunks when
   `DeltasNarrowOn`) against the shadow copy with NEON (`queued.cpp` `CaptureDelta`).
2. **Why it costs.** 150–540 draws a frame × up to 13 KB = 2–7 MB of reads a frame, on the main thread, evicting game
   data from the 32 KB L1.
3. **Solution.**
   - Mark chunks dirty in FF's D3D setter functions, whose addresses are already hooked/remapped (`out/ff_hook_map.txt`).
     FF's statically linked D3D writes the device from guest code, so either hook those writers or derive marks from
     the store cards with finer granularity over the device range only.
   - Fall back to the full compare when a write outside the known setters is seen (counter in verify mode).
4. **Files.**
   - `app/src/renut_engine/gpu/backend/queued.cpp`.
   - `d3d/d3d_native_state.cpp`.
   - `d3d/hooks/*.cpp`.
5. **Core 2.** ≤2.8 % direct, plus cache effects.
6. **Switch.** High: L1 size and memory bandwidth (about 8 GB/s measured on the Switch, EXTERNAL).
7. **Difficulty.** Medium–high: the device writers must be complete.
8. **Regressions.** A missed state change means a wrong draw. `sa2_renut_snapshot_deltas_verify` exists and must stay
   at 0 differ (k205 precedent).
9. **Benchmark.** CaptureDelta samples; verify run differ count 0; draw output identical.
10. **Profiling first?** A fresh k250v stack profile.

### 5.6 Shader variant cache completeness (rank 5)

1. **Problem.** Battle variants missing from rc9 are compiled at run time by uam: 22 % of all process samples in
   k247s.
2. **Why it costs.** Large CPU bursts (NVIDIA's nv50 compiler) and first-use stalls.
3. **Solution.**
   - Merge the variants the console has compiled since rc9: the SD `deko3d_shaders.bin` grows, and the k248 version of
     `perf_run.ps1` fetches it as `deko3d_shaders_live.bin`; that version is not on the PC yet.
   - Build an rc10 cache with `tools/deko3d_variant_cache.py --merge … --runtime-log`.
4. **Files.**
   - `tools/deko3d_variant_cache.py`.
   - `tools/switch_tests/perf_run.ps1`, which today redeploys rc9 before each run, so every test pays the compiles.
5. **Core 2.** Indirect; the compiles run on core 3 now.
6. **Switch.** Removes stalls in play.
7. **Difficulty.** Low.
8. **Regressions.** Cache size (505 MB already).
9. **Benchmark.** "pipelines created in the interval" and uam samples → 0 in battle.
10. **Profiling first?** No.

### 5.7 Native XAudio mixer for FF (rank 7)

1. **Problem.** FF's audio XThread spends 27 % in sub_82BB4008 and 25 % in sub_82A34BB0 (k247s), and runs at 27–37 %
   of a core in battle.
2. **Why it costs.** Recompiled VMX mixing, with flush-mode switches, context traffic and byte swaps.
3. **Solution.**
   - Identify both functions: compare their instruction bytes with N&B's `XAUDIO::SRC::LinearVector::ProcessBlock`
     (0x82666D60) and the reverb that `banjo_xaudio_native.cpp` / `banjo_reverb_native.cpp` implement. FF and N&B
     share the XDK generation.
   - Map them for FF, or write FF natives with the same verify mode.
4. **Files.** `app/src/cpu/banjo_xaudio_native.cpp`, `banjo_reverb_native.cpp`; FF hook registration.
5. **Core 2.** Indirect: less 0x3B load on cores 0–2.
6. **Switch.** High.
7. **Difficulty.** Medium.
8. **Regressions.** Audio differences, caught by the verify mode (bit-exact compare).
9. **Benchmark.** Audio thread CPU %; audio requests without data = 0.
10. **Profiling first?** No.

### 5.8 PGO retraining and build A/Bs (rank 8)

1. **Problem.** The profile is one 300 s scripted battle (k250t). The field, menus, cutscenes and other battles run
   without a profile (`-fprofile-partial-training`).
2. **Solution.**
   - Several training sessions merged (`tools/pgo_merge.py` takes multiple `banjo_pgo_N.stream`).
   - Then A/B `BANJO_SWITCH_IPA_PTA` and the inliner limits (`BANJO_SWITCH_INLINE_AUTO` / `_UNIT_GROWTH`).
3. **Expected.** MEDIUM: one session gave +11 %.
4. **Risk.** Low. Build time about 30 min per use build.
5. **Benchmark.** Console A/B; check the scenes not trained so far.

---

## 6. Recommended implementation order

**Phase 1 – Immediate, low risk (measure and remove obvious work).**
1. Confirm FF's codegen flags (non_volatile / non_argument / args_in_registers / fp_moves_modeless) and the toolchain's
   `-mtune`.
2. Stack-profile the k250v build in the heavy phase. Add per-frame main-thread run / wait time and the waited objects.
3. PC census of FPCR writes per frame in the battle.
4. Complete the shader cache (rc10) and make perf runs keep the console's grown cache.
5. Copy boundary pruning (5.3), verified by the guard.

**Phase 2 – Structural CPU improvements (runtime and renderer).**
1. Act on the wait / scheduling data (5.4). A priority or placement change only with the user's approval.
2. CaptureDelta dirty tracking (5.5), with the verify run at 0 differ.
3. Native XAudio mixer for FF (5.7).
4. ObjectTable read path, CheckTitleTermination, memmove callers.

**Phase 3 – Recompiler optimization (generated C++).**
1. Whole-program registers as locals (5.1): exclude list, guard, PC comparison, console A/B.
2. FPCR switch reduction (5.2): (b) → (c) → (d).
3. Next round of copies / hand natives from the post-Phase-3 profile (the profile changes after 1 and 2).

**Phase 4 – Switch-specific.**
1. TLB study: a microbenchmark of guest heap access patterns with 4 KB vs larger mappings, if Horizon allows larger
   mappings for the guest window (MISSING: what libnx / svc permits for this reservation).
2. Alignment rules from the A57 guide: loads crossing 64 bytes and stores crossing 16 bytes are penalized. Check the
   guest stack frame and vector store alignment in hot copies.
3. Revisit the four-core layout only after the work removal (it moves work; the rules say remove first).

**Phase 5 – Compiler and build (user-authorized PGO/LTO already granted).**
1. Multi-scene PGO training.
2. A/B `-fipa-pta`, inliner limits.
3. Keep `-ffp-contract=off`, `-fno-strict-aliasing`, early-RA off (correctness).

---

## 7. Profiling plan

All console runs: handheld, CPU 1020 / GPU 460.8 / EMC 1331.2 MHz (the profiler prints the clocks every block),
Frame Tap off, the same save tile, the same scripted 300 s battle (`perf_run.ps1`).

| Quantity | How |
|---|---|
| Core 2 / per-core utilization | `rex_profile.log` "system core 3: idle" plus the overlay. Report per core in addition to per thread: add the per-core idle tick deltas that the overlay already reads to the 10 s block. |
| Per-thread CPU | `rex_profile.log` thread lines (CPU %, mask, priority). Already there. |
| Function-level CPU | `switch_profile_stacks` (1 ms sampler) → `tools/switch_hot_symbols.py` with the exact ELF; main thread and all threads (`_main`, `_all`, `_x`). |
| Main-thread time per frame | CPU % × 10 s / frames in the block (e.g. k250v heavy phase: 30–45 ms; 30 fps phase: 17–20 ms). Add a per-frame record (run time, wait time, wait object) in the profiler. |
| Render-thread time | `[renut] … render thread busy X ms a frame` (k250v gameplay blocks: up to 13.5 ms). |
| Total CPU | "CPU total N % (400 % = 4 cores)" → **core-ms per frame** = N % × 10 s / frames. |
| Frame time | `[renut] 10.0 s: N game frames`; the overlay frame-time graph; min / 10th percentile of the blocks. |
| CPU / GPU sync | `[renut] … GPU X ms a frame`, the dynamic-resolution line, libnx "fences waited", "window queue / dequeue buffer ms". |
| FPCR writes | PC census (`REX_SYS_CENSUS`) per frame; console: samples within the first instructions after `msr fpcr`, if the sampler's PC can be matched. |
| Switch at 1020 MHz | Every run's header line ("clocks: CPU 1020.0 MHz …, Reverse-NX … obeying handheld"). Reject a run whose clocks differ. |

**Telling removed work from moved work.**
- Report three numbers for every A/B:
  - main-thread ms per frame;
  - total core-ms per frame across all threads;
  - battle fps.
- Removed work lowers both main-thread and total core-ms. Moved work lowers the main thread while total core-ms stays
  the same or rises.
- Example: compile core 3 lowered cores 0–2 load but not total CPU, so it is a move.
- Use the same scenes, compare heavy-phase blocks with heavy-phase blocks, and take at least two runs per side
  (block-to-block variance in k250v is ±3 fps).

---

## 8. Sources and references

| Source | Why it is relevant |
|---|---|
| [XenonRecomp README](https://cdn.jsdelivr.net/gh/hedge-dev/XenonRecomp@main/README.md) | Register-as-local options; "the largest gains"; removing save/restore helper calls; Unleashed Recompiled frame-time effect. |
| [rexglue SDK](https://github.com/rexglue/rexglue-sdk) and its [optimization page](https://www.mintlify.com/rexglue/rexglue-sdk/advanced/optimization) | The SDK this port uses: codegen flags, LTO, strict FP flags. |
| [Arm Cortex-A57 Software Optimization Guide](https://documentation-service.arm.com/static/5ed75ee1ca06a95ce53f93c0) | 3-wide dispatch; one load + one store per cycle; penalties for loads crossing 64 B and stores crossing 16 B; FP multiply-accumulate latencies. |
| [Chips and Cheese: Cortex A57, Nintendo Switch's CPU](https://chipsandcheese.com/p/cortex-a57-nintendo-switchs-cpu) | 12-entry store queue; no memory disambiguation; ≈7-cycle store forwarding; 32-entry DTLB; 2 MB L2 at 22 cycles; Switch DRAM below 8 GB/s measured; 128-entry register file. |
| [SwitchBrew: Tegra X1](https://switchbrew.org/wiki/Tegra_X1) | Cache sizes (48 KB L1I, 32 KB L1D, 2 MB L2), LPDDR4 25.6 GB/s theoretical. |
| [yuzu GlobalSchedulerContext header](https://git.dawidpotocki.com/mirror/yuzu-mainline/plain/src/core/hle/kernel/global_scheduler_context.h), [yuzu preemption timing commit](https://git.openpunk.com/OpenPunk/yuzu/commit/894ed14ebc0ca938f2f240f37a5d8f1eda4cd66f) | Horizon's preemption: run queues at a preemption priority rotated every 10 ms (the basis of the 0x3B hypothesis). |
| [Atmosphère mesosphère KScheduler](https://github.com/Atmosphere-NX/Atmosphere) | `RotateScheduledQueue` per core and priority: the console kernel's mechanism. |
| [deko3d](https://www.github.com/devkitPro/deko3d), [deko3d pitfalls (RSDuck)](https://gist.github.com/RSDuck/b60d89fbcb4374977cb8fbc7fccdbde4) | Engine switches insert waits and cache flushes; barriers are not free; flush semantics. For renderer-side CPU work. |
| [GCC AArch64 options](https://gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/AArch64-Options.html) | `-mcpu` / `-mtune=cortex-a57`, outline atomics, early RA. |
| [WPA: critical path and wait analysis](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/optimizing-performance-and-responsiveness-exercise-3), [high context switches (GDK)](https://learn.microsoft.com/en-us/gaming/gdk/docs/gdk-dev/console-dev/overviews/threads/high-context-switches) | PC-side ready-time and wait-chain analysis without new downloads. |
| [UnleashedRecomp-NX](https://github.com/givethesourceplox/UnleashedRecomp-NX), [GameBrew page](https://www.gamebrew.org/wiki/Unleashed_Recompiled_Switch) | Comparable XenonRecomp port on the Switch: needs low resolution, and an overclock is "strongly recommended". Shows the A57 is the limit for 360 recompilations. |
| [N64Recomp](https://github.com/N64Recomp/N64Recomp) | Static recompilation to C with the host compiler doing register allocation: the same principle as rank 1. |
| In-tree: `sdk/include/rex/ppc/diet_ops.h`, `context.h`, `sdk/src/codegen/builders/*`, `tools/localize_native.py`, `app/src/renut_engine/gpu/backend/queued.cpp`, `docs/diagnostics/PERF_30FPS.md` | Implementation facts quoted above; sa2-nx's FPCR finding; the copy mechanism; CaptureDelta. |

---

### Session note

The PC was turned off during the session. Evidence came from logs, profiles and source staged before that, plus the
cloud copy of the source. Items marked INFERRED or MISSING must be checked on the PC tree before any implementation.
Nothing here has been implemented; the user's review and approval come first.
