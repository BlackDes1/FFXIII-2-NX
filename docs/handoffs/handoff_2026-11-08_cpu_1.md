# Handoff 1: distributed CPU work, FFXIII-2 on the Switch (2026-11-08, 11:10-13:10 UTC)

Work order: docs attachment "Comprehensive Distributed CPU Optimization" (no PGO, no LTO; console decides; exact;
remove work, do not move it). Emulator runs: Ryujinx in handheld mode (the user, 2026-11-08), as a proxy only.

## Baseline (console, k257 = current baseline, New Bodhum field, handheld, stock 1020 MHz)
- Frame 60-70 ms (14-17 fps) in the heavy steady phase; render thread 13-14 ms a frame, GPU 30-34 ms a frame.
- Main Thread: 84 % of a core (rex_profile thread CPU); 21 % of its 1 kHz samples are "waiting in the kernel".
- Sampled run k260_diag (39,356 samples, ELF out\k260\renut): see docs\bottlenecks-2026-11-08.md.
- Not measurable on this console without new tooling: instructions retired, cycles, CPI, cache misses (the SDK has
  no PMU access; Horizon does not expose the A57's PMU to applications). Proxies used below.

## What the 83 % means
Samples whose pc is in recompiled game code (generated functions or their native copies), all threads of the
sampled scene, main thread alone 82.7 %. The rest: renderer front-end on the game thread 6.5 %, kernel / sync 5.3 %,
libc 1 %.

## Systemic findings (all 1,391 functions, not one hot spot)
1. Memory latency: the instruction at the sampled pc is a load in 58 % of the main thread's samples (50 % integer, 8 %
   vector), a store in 21 %. The A57 retires in order behind an outstanding load: the thread is load-latency bound.
   The game's own data structures dominate; our share: PPCContext (ctx) traffic of generated code, the guest
   register save/restore helpers (__savegprlr/__restgprlr 2.8 % of the main thread by themselves), store cards.
2. Register spills to ctx: e.g. sub_8228EFC0 (4x4 matrix product, VMX) writes 10 vector registers back to the ctx
   (str q, [x0, #1488..1600]) on every call; non-volatile registers r14-r31 / f14-f31 / v live in ctx in every
   generated function (the "register diet" was OFF in this tree).
3. Store cards: ~170,000 guest store sites also write a card byte (REX_CARD: one more store to another cache line).
   They pay off on the render thread (3.5 GB of stream hashing avoided), but every heap store of the game pays.
   Cost not measured yet (needs a build with REX_STORE_CARDS_OFF and a console A/B).
4. Waits of the main thread (its kernel samples): 52 % NtWaitForSingleObjectEx in copy_82B69618 (frame sync with
   another thread), 13 % KeDelayExecutionThread -> sched_yield (a guest Sleep(0) poll, copy_82B69868), 9.4 %
   NtReleaseMutant -> condition_variable::notify_all (a wake syscall with nobody waiting), 3-5 % mutex / ObjectTable
   locks, 2 % render-queue notify.
5. Correctness: the copy guard (Ryujinx k262_check) found one machine copy that is not exact: sub_828FB3E8
   (recursive; its saved r30/r31 slots on the guest stack differ). It was in the bulk list since k255 (guard off on
   the console). Removed in k263; k263 in handheld: 860 copies reached, 34,672 calls compared, 0 differences.

## Implemented
| Build | Change | Status |
|---|---|---|
| k262 | machine copies round 2 from the k260 console profile (2,111 copies, 817 new) | emulator guard: 1 bad copy (old) |
| k263 | sub_828FB3E8 out of the bulk list; NtReleaseMutant without a wake syscall when nobody waits (threading_posix.cpp, as events/semaphores already do) | built; emulator handheld: 0 differences |
| k260 | SDK sampler honours switch_profile_stacks from the config (was read before the config) | in use |
| k264 | register diet: non_volatile_as_local + elide_lr_stores (+ audit marks) | codegen + full build running |

Diet audit (read_before_write on a codegen with the options): 21 findings -> 2 split pieces share_registers
(0x82505B90, 0x82A53108), 10 vector registers kept in ctx (v29, v117, v119-v122, v124-v127), 4 left that are exact
(xor r30,r30,r30 x2; a carry independent of r29; setjmp). Backups: app\generated_before_k264,
app\generated_zones_before_k264, app\renut_config.toml.before_k264, app\config\renut_funcs.toml.before_k264.

## Measured / rejected so far today
- Bulk copies on vs off, same console session (k257): +1.0 fps (+4 %), 18 of 23 pairs.
- A (XMA decoder on core 3): +0.35 fps, noise. B (main thread alone on a core): worse. Both off.
- Overlay costs 0.4 fps (3 %).
- Ryujinx fps (11-12 in the first battle) does not move with the copies: the emulator, not the guest code, limits.
  Emulator runs check correctness only.

## Next (in order)
1. k264: emulator handheld run with the copy guard (boot, battle, 0 differences), then console: same scene, main
   thread CPU per frame and fps vs k263 (two sessions, compile-time change).
2. Store cards cost: build with REX_STORE_CARDS_OFF (renderer then hashes streams), console A/B of main thread vs
   render thread time. Keep only if the main thread gains more than the render thread loses at the frame level.
3. non_argument_as_local: needs a raw codegen without the option for tools/non_argument_audit.py (the post-codegen
   spellings confuse it: 8.9 MB of false findings on app\generated).
4. Guest Sleep(0) poll (copy_82B69868 -> KeDelayExecutionThread): find what it waits for; a wake instead of a yield
   loop saves CPU only if another thread on that core needs it.
5. ObjectTable lookups under a lock (contended 5 %): a lock-free read path.
