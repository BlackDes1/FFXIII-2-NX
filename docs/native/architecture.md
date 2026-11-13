# Architecture report — FFXIII-2 native Nintendo Switch port (first deliverable)

Session 1, 2026-11-08. Basis: `docs/previous-work-analysis.md`. Labels: MEASURED / CODE / ESTIMATE (theoretical,
not measured) / HYPOTHESIS. **No number marked ESTIMATE is a promise.**

---

## 1. What "native" should mean for this project

The existing port already executes the game as AArch64 machine code (static AOT recompilation, no interpreter, no JIT).
So "native" is not a change of execution method; it is the removal of per-operation costs that come from modelling
the Xbox 360 instead of running the game's logic directly:

| Layer | Today (ReXGlue/Renut) | Native target | Keep / change |
|---|---|---|---|
| Instruction execution | AArch64 from generated C++ | same | keep (static AOT) |
| Guest registers | partly C++ locals (cr/ctr/xer, non-volatiles since k264), rest in `PPCContext` memory | **all registers SSA/locals inside a function**; ctx only at ABI-unknown boundaries | change (P1) |
| Calls | `sub_X(ctx, base)`, args/results through ctx memory | `__fast_sub_X(args…) → r3/f1/v2` by value for direct calls; ctx wrapper only for indirect/hooked entries | change (P1, SDK pass exists) |
| FP mode | FPCR.FZ toggled at VMX↔FPU boundaries (41-44 cycles each) | mode decided per function region; isolated VMX ops in IEEE mode with bit-exact software flush | change (P2) |
| Guest memory | flat window, big-endian, `rev` per access | same (BE data files and layouts make LE conversion infeasible; `rev` is cheap) | keep |
| Memory barriers | asm memory clobber of all guest memory at every loop header (17,363) | only where the loop really polls shared memory | change later, high risk |
| Store cards | extra `strb` per guest store site | only for GPU-visible ranges, or replaced by write-tracking in native D3D paths | change after measurement |
| Kernel objects / sync | HLE xboxkrnl objects in an ObjectTable under a global mutex; waits via host primitives | Horizon primitives directly, lock-free handle lookup, no wake syscalls without waiters | change incrementally |
| D3D | FF's own D3D library hooked; per-draw device snapshot + compare (CaptureDelta) on the game thread; render thread translates | native D3D replacement records state changes directly (dirty bits from the setters); render thread unchanged in role | change (P3) |
| GPU backend | plume → deko3d, Xenos shaders translated offline, DKSH cache | deko3d directly where plume adds per-call overhead; same shader semantics; complete offline variant set | keep + trim |
| Audio | guest XAudio mixer (recompiled VMX) + HLE XMA decoder | native mixer/SRC/reverb (bit-exact verified), native XMA | change (P4) |

**Not proposed:** a JIT/dynarec (no advantage over AOT on Horizon), converting guest memory to little-endian, a full
rewrite or decompilation of 50,196 functions, reproducing the PS3's SPU model.

## 2. The two walls (why both CPU and GPU must move)

| Wall | Need | Today | Gap |
|---|---|---|---|
| CPU: game main thread | ≤ ~28 ms CPU per frame (33.3 ms budget minus sync/jitter headroom) | 30-45 ms heavy phases, 17-20 ms light (MEASURED k250v; that build had PGO+LTO, which this project may not use: without it ≈ +10 %) | −25 % to −40 % in heavy phases |
| GPU at pixel-identical 1280×720 2×MSAA | ≤ ~31 ms | 30-34 ms field, 35-39 ms battle, opening scene up to 54-61 ms (MEASURED) | −10 % to −20 % in battle; much more in the opening scene |

Pixel-identical rules out dynamic resolution as a default, so the GPU wall is real for this project.

## 3. Answers to the work order's questions

### 3.1 What can be reused safely?
- rexglue **frontend**: XEX loading, PPC decoding, function discovery, jump-table/vtable/gap analysis, all crash-class
  fixes (codegen-crash-fixes.md), VMX semantics fixes (vmaxfp/vminfp NaN/±0), `-ffp-contract=off`, NEON VMX helpers.
- **Kernel HLE** behaviour fixes: XMA timer (k196), 64-bit export args, NtReleaseMutant wake fix (k263), spin backoff
  for the 22 polling loops, critical-section fast path.
- **Renut renderer semantics**: bind-time header latching for every binding (k241 `stable_all`), tiling identity by
  origin (k229), copies keyed by header+contents (R4), SA-1 composed shadow atlas (k243d), push constants off,
  async variants, DKSH cache tooling.
- **Verification tooling**: copy guard / `banjo_cpu_verify`, `read_before_write.py` audits with noreturn seeds,
  `args_in_registers` instrument/poison modes, snapshot-delta verify, renderer read-back (`banjo_video_s` +
  `[capframe]` + `[rtlog]`), console remote tools (deploy / launch / collect), the 1 ms stack sampler + symbolizer.
- Licences: GPL-3.0 parts (renut-nx, katamari-nx, sa2-nx, …), BSD-3 SDK parts; reNut's author permits reuse. The new
  project must be GPL-3.0 and keep the credits.

### 3.2 What should be independently reimplemented?
1. Code emission defaults: the whole-program register/argument model (by enabling and hardening the SDK passes, not by
   writing a new recompiler).
2. FP-mode handling (per-function mode summaries, software flush for isolated VMX ops).
3. The game-thread D3D front end (native state recording instead of snapshot+compare).
4. The audio mixer path (native, bit-exact).
5. Kernel object lookup and the frame-sync wait path (after measuring what the main thread waits for).

### 3.3 Which compatibility layers are unnecessary?
- Per-draw full device snapshot comparison (CaptureDelta) — the setters know what changed.
- ctx stores/loads for arguments and returns at direct calls between ordinary functions.
- `__savegprlr/__restgprlr` (already gone: 13,514 → 18 calls with k264).
- Wake syscalls with no waiter (mostly fixed), global-mutex ObjectTable lookups.
- Store cards for CPU-only memory (if the GPU-visible set can be bounded).
- The emulated Xenos PM4 GPU and its EDRAM model are not needed on the Switch path (keep only as a PC diagnostic).
- plume's abstraction on hot per-draw paths (to be measured: render thread is 13-14 ms and not the limit today).

Not unnecessary: big-endian access, the guest heap layout, the loop barriers (until each polling loop is proven).

### 3.4 Best native ARM64 execution strategy
**Static AOT recompilation, upgraded to an "ABI-lifted" form**:
1. `args_in_registers` (interprocedural Par/Fw/Ex/Val dataflow already in `sdk/src/codegen/args_in_registers.*`,
   reference script `tools/args_in_registers.py`) — direct calls pass r3-r10/f1-f13 by value and return r3 by value.
2. `non_argument_as_local` + `non_volatile_as_local` — no register stays in ctx inside a function.
3. Exclude lists generated, not hand-written: noreturn seeds (0x82C15900, 0x82C150C8), setjmp users, SEH funclets,
   save/restore helpers, hooked functions, functions the read-before-write audit flags.
4. Verification by construction: `args_in_registers_instrument`/`_poison` on PC (poisoned ctx fields must never be
   read), copy guard on hot functions, then console A/B in one session.
5. Then FP-mode summaries (P2) on top, because they use the same interprocedural machinery.
6. Hand natives only where a function exceeds the 2 % gate after 1-5 (re-profile first).

Why this beats the alternatives: the profile is flat (top 100 = 40 %), so only code-generation-wide changes reach the
whole 83 % game-code share; GCC can only keep values in A57 registers if they are not fields of a memory object
that every `-fno-strict-aliasing` guest store may alias. That aliasing is a plausible root cause of the "58 % of
samples at loads" finding (HYPOTHESIS, to be checked in the P1 disassembly).

### 3.5 Best graphics architecture for pixel-identical rendering
- **Keep the HLE-D3D approach** (hook FF's D3D library, drive deko3d natively). PM4/EDRAM emulation costs far more CPU
  and GPU bandwidth on a 25.6 GB/s device than a 360 with 256 GB/s eDRAM-internal bandwidth.
- Make the rendering invariants (`docs/rendering.md`) the contract: bind-time latch, identity by guest semantics,
  EDRAM persistence, view aliasing where the game relies on it, resolve ordering.
- Move state capture to the native D3D layer (dirty bits) — saves main-thread CPU, does not change pixels when the
  verify mode shows 0 differences.
- GPU time without pixel change, measured per pass (`renut_gpu_profile`): the heavy passes are known (radial blur
  14 ms on RGBA16F in the opening, smoke 2-6.5 ms, a 4 ms opaque pass with a cube map, full-screen 2.2 ms). Candidates:
  shader code quality of the XenosRecomp→SPIRV-Cross→GLSL path (instruction count, register pressure, fp16 where the
  360 shader was fp16-exact — only if bit-identical), avoiding redundant resolves/copies, tile-friendly pass order on
  Maxwell, framebuffer compression for colour targets (depth compression was measured worse). Every candidate is
  accepted only with a bit-exact frame comparison against the reference renderer output.
- "Pixel-identical" needs an operational definition for regression testing: identical to the frames of the current
  verified Renut build in a fixed set of scenes (the 360 reference itself — xenia/real hardware — is the semantic
  ground truth for disputes). Proposed in `docs/rendering.md`.

### 3.6 Which CPU bottlenecks are likely to matter most?
1. Generated-code execution cost on the game main thread (load latency, ctx traffic, call boundaries). MEASURED
   share 82.7 %; mechanism HYPOTHESIS.
2. FPCR switches (MEASURED cost, count UNKNOWN).
3. Renut front-end on the game thread (6.5 %, CaptureDelta 2.2-2.8 %).
4. Kernel/sync on the game thread (5.3 %) and *what* it waits for (frame sync 52 % of its kernel samples).
5. Shared cores 0-2: audio mixer 27-37 %, XMA 9-23 %, effect workers 4-24 % compete with the main thread at the
   same priority 0x3B (HYPOTHESIS: lost 10 ms preemption slices).

### 3.7 Largest architectural risks
| Risk | Why | Mitigation |
|---|---|---|
| R-A Even an ideal CPU codegen may not reach ≤ 28 ms in heavy battles | flat profile, A57 at 1020 MHz, UnleashedRecomp-NX needs an overclock for a similar game (EXTERNAL) | measure P1 early; be honest with the user; keep optional settings separate |
| R-B GPU at 720p 2×MSAA exceeds budget without visual change | 35-39 ms measured; bandwidth-bound effects | per-pass profiling + bit-exact shader/pass work; if not enough, the user decides on options |
| R-C ABI-lifting correctness | functions that break the PPC ABI (asm, setjmp, SEH, split functions, noreturn paths) — the k264 crash was this class | generated exclude lists, instrument/poison runs, copy guard, staged rollout |
| R-D Isolation vs. tooling | the console/remote tools and the PC job runner live in the old tree and are in use by another session right now | own job runner + copied tools in the new project; never two builds at once |
| R-E PS3 reference not readable | PS3 EBOOT is an encrypted SELF; decrypting needs tools not installed (download = permission) | use PS3 data files only; ask before any tool download |
| R-F Build time / NRO size | 50k functions, 90 MB NRO; whole-program passes multiply codegen time | sharding as today; measure |
| R-G Licensing | mixed GPL-3.0/BSD sources | new project GPL-3.0, credits file from day 1 |

### 3.8 Which prototype first?
**P1 — ABI-lifted codegen for FF** (enable `args_in_registers` + `non_argument_as_local` on top of the k264 diet, in
the new project). Reasons: largest expected CPU effect; the code exists and is in use in other ports; zero visual
risk if exact; it is the foundation P2/P3 build on. Details and gates in `docs/migration-roadmap.md` (Phase 2).

Refinement found later in session 1: in the k260 profile 61.6 % of the main thread already runs in machine copies /
overrides ("hooked" 32.8 % + "copy" 28.8 %), and `args_in_registers` keeps excluded (overridden) functions on the old
ABI. P1 is therefore tested as **P1a** (copies retired, only the 286 real hooks excluded: one uniform whole-program code
form) and P1b (copies kept). The expected gain applies to the 82.7 % game-code share only in P1a.

### 3.9 Realistic path to stable 30 fps
ESTIMATE (low confidence; replaced by measurements as they arrive), heavy-phase main thread ≈ 38 ms without PGO:

| Step | Main-thread effect (ESTIMATE) | After |
|---|---|---|
| P1 ABI-lifted codegen | −10 % … −20 % | ~31-34 ms |
| P2 FPCR summaries + software flush | 0 … −8 % (unknown until census) | ~29-33 ms |
| P3 native D3D state recording | −2 … −5 % | ~28-32 ms |
| P5 sync/kernel fast paths, card narrowing | −2 … −5 % | ~27-31 ms |
| Re-profile → hand natives ≥ 2 % gate | −2 … −5 % | ~26-30 ms |

Light and medium scenes (17-25 ms today) would reach 30 fps early; heavy battles land around the budget edge — not a
guaranteed 30. In parallel the GPU must lose 10-20 % in battles without pixel change. If the measured results stay
short, the honest outcome is: stable 30 fps pixel-identical in most of the game, drops in the heaviest scenes, and
the user decides whether optional settings (dynamic resolution, PGO/LTO) are allowed for those.

## 4. Ranking of proposed changes

Columns: expected performance improvement · confidence in the estimate · implementation complexity · compatibility
risk · visual regression risk.

| Rank | Change | Perf (main thread unless noted) | Confidence | Complexity | Compat risk | Visual risk |
|---|---|---|---|---|---|---|
| 1 | P1 args_in_registers + non_argument_as_local (whole program, generated excludes) | HIGH (10-20 %) | low-medium | medium (SDK pass exists) | medium-high | none if exact |
| 2 | Complete DKSH variant set (no run-time uam in play) | MEDIUM (stalls; frees core time) | high | low | low | none |
| 3 | P2 FPCR per-function mode summaries + bit-exact software flush | 0-8 % (census first) | low | medium | medium | low (denormals) |
| 4 | P3 native D3D state recording (dirty bits, retire CaptureDelta) | 2-5 % + less L1 pollution | medium-high | medium-high | medium | medium (missed state; verify mode required) |
| 5 | GPU per-pass work (shader quality, redundant resolves, compression) | GPU 10-20 % (battle) | low-medium | high | low | **medium** (only bit-exact accepted) |
| 6 | Main-thread wait analysis (frame sync, Sleep(0) poll) → remove needless waits | 0-10 % of frame time | low | low to measure | low | none |
| 7 | Native audio mixer (bit-exact) | frees 10-15 % of a core on cores 0-2 (indirect) | medium | medium | low-medium | none |
| 8 | Lock-free ObjectTable / kernel fast paths | 1-2 % | medium | low | low | none |
| 9 | Store-card narrowing to GPU-visible ranges | 1-2 % | medium | low-medium | low | low (stale buffers if wrong) |
| 10 | Hand natives after re-profile (≥ 2 % gate) | 2-5 % total | medium | low each | low | none |
| 11 | Loop-barrier narrowing | unknown | low | medium | **high** (hangs) | none |
| — | PGO/LTO, dynamic resolution | +11 % / GPU −25 % | high | done before | low | dynres: **not pixel-identical** | excluded by the work order unless the user re-allows |

## 5. Target structure of the new project (planned)

```
FFXIII-2-Native-Switch/
  Claude.md                  continuity file
  docs/                      architecture, roadmap, performance, rendering, isolation, progress-log, handoffs/
  research/                  PC / PS3 / reference-port notes
  codegen/                   copied rexglue frontend + codegen config for FF (P1)
  src/                       runtime (copied SDK parts), native subsystems, renderer (copied Renut, trimmed)
  prototypes/                isolated experiments (each with README: hypothesis, test, result)
  tools/                     copied/adapted verification + console tools, own job runner
  profiling/, logs/          measurement outputs
  build/                     all build outputs (never inside Works\)
```

## 2026-11-12 / 13
UI targets (render/surfaces.cpp: `g_ui`, `g_ui_depth`, `g_ui_menu`; render/ui_scale.cpp), the blank thread's early
blank (ffxiii2_renut_glue.cpp `NoteSwapForVblank`), shared clock sessions (sdk/src/ui/switch_apm.cpp):
[session-2026-11-12-13.md](session-2026-11-12-13.md).
