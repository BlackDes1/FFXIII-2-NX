# Threading check and reference-repo optimisation pass (2026-11-08, k256/k257)

Baseline: k255 (Works\builds\2026-11-08_0819_ffxiii2_k255_baseline, dynres off, shadow fix on). Console, handheld, stock
clocks (CPU 1020 MHz, GPU 460.8 MHz). Rule kept: an optimised function does exactly what the recompiled one does.

## 1. Threading / synchronisation on the Switch's ARM cores (audit, nothing changed)

What the k255 console logs show (rex_profile.log, renut_*.log):

- Four-core mode is active: process mask 0xF, banjo_system_core 1. Core 3 holds the audio output (0x2A) and the Audio
  Worker (0x2B); it was ~51 % idle in the battle-heavy blocks. All game threads run on cores 0-2 (mask 0x7) at 0x3B.
- Frame limiter in the field/battle: the guest Main Thread, 82-83 % of a core in every loaded block. Second: one
  XThread at 50-65 %. The render thread is busy 4-9 ms a frame; the GPU 15-25 ms; so the CPU main thread decides.
- XMA Decoder: 9-23 % of a core, on cores 0-2 with the game (banjo_system_core_xma = false).
- Guest atomics are correct and cheap: lwarx/stwcx. (480+55 sites) become REX_GUEST_CAS (ldaxr/stlxr, no trailing
  dmb: banjo_cpu_guest_cas_no_barrier ON); lwsync (371) -> acq_rel fence; sync (32) and eieio (14) -> seq_cst fence;
  17,363 loop headers carry a compiler-only barrier (needed because guest loads are not volatile). This matches the
  PowerPC model (nothing weaker than the guest asked for). No change needed.
- Waits: thread_wait_blocking, thread_signal_skip_idle (10,334 signals/s skipped a wake syscall),
  kernel_critsec_pcr_owner, log_file_writer, the effect workers' spin backoff (ffxiii2_spin_backoff) are all ON.
- Remaining sync costs seen: ~28,600 yields/s (sched_yield, from NtYieldExecution / spinlock / XObject paths),
  ObjectTable lookups 55,700/s with 2,788 contended (5 %), 820 critical-section host waits/s. In the sampled profile
  each of these is below 1 % of the main thread (LookupObject 0.4 %, RtlEnterCriticalSection 0.4 %, mutex 1 %).
- perfil_pilas.flag / stacks_profile.flag are not on the SD and the k255 toml leaves switch_profile_stacks false: the
  stack sampler is already off ("CPU sampler off" in the log).

Not done, needs the user's approval (they move work between cores or change scheduling):
- A. banjo_system_core_xma = true: the XMA Decoder (9-23 % of a core) to core 3, which has ~50 % idle.
- B. Main thread on an exclusive core (masseffect-nx's masseffect_exclusive_core idea): the Main Thread shares
  cores 0-2 with ~30 threads at the same priority 0x3B.
- C. A lock-free / sharded ObjectTable lookup (exact; small, < 0.5 % of the main thread).

## 2. What the reference repos had that we did not

Most of the keflings/banjo-nx codegen pipeline (all 33 post-codegen steps), the four-core mode, the ring/renderer
keys and the hand-written natives are already in our tree. Read and checked: mkdd OPTIMIZATION + DEKO3D_MIGRATION,
LostOdyssey SWITCH-PGO (first part), mk9 census, kameo optimising-functions, keflings codegen-enhancements,
cpu-codegen-proposals (first 100k chars), enhancements, four-core-mode, pgo-lto, renut-cpu-work-from-sa2,
masseffect audio-cpu, cpu-cost-analysis, best-config, streaming-io, katamari optimising-functions,
nfsmw performance-history, measuring. Not read (rate limits / time): the remaining keflings renderer and shader
docs, NetherSX2_nx, pacman, renut-nx docs, the other masseffect docs, torchlight patch, katamari sdk tree,
nfsmw native-renderer, platform-notes, porting-another-game.

Taken over:
- kameo/katamari: bulk machine copies of every function seen running on the console, rewritten with --merge, read
  with the ELF of the exact build that wrote the profile. Done (k256), see 3.
- nfsmw "measuring": compare inside one session with an alternating A/B, not two runs (random encounters made a
  two-run comparison show +29 % that was not real). Done: in-run A/B target 20 (k257).

Not taken (reason):
- sa2-nx renderer CPU items (snapshot deltas, chain reuse, wake batches): our render thread is 4-9 ms a frame and not
  the limit.
- masseffect heap_free_bitmap: BaseHeap::AllocRange does not show in our main-thread profile.
- masseffect streaming block cache: produced wrong bytes on their console.
- PGO/LTO: only on the user's word.

## 3. k256 / k257: more machine copies, and their in-run A/B

- tools/switch_hot_bulk.py on the k246b_2 console profile (+ the c9ead929bf24 ELF), --merge --min-samples 1:
  756 functions added; localize_native.py --list --bulk: 1,296 bulk copies in 12 shards (4 refused: raw guest
  access). localized_bulk.txt.before_k256 is the old list (545). NRO 76.4 -> 84.6 MB.
- k257 adds banjo_renut_ab_target = 20 (banjo_cpu_native_bulk): every bulk hook runs its recompiled original in the
  off half (SetBulkAbOff in src/cpu/banjo_hot_guest.cpp; backups *.before_k257). Both halves are the same game.
- Console, one session, 5 s halves (renut_k257_ab_bulk.toml, logs/switch/20261108_0914_195fc5083ba0/k257_ab_2):
  steady scene (both halves GPU >= 24 ms, 23 pairs): bulk ON 26.2 fps mean (26.1 median), OFF 25.2 (25.4);
  ON better in 18 of 23 pairs, +1.0 fps mean (+4 %). All blocks after loading: 26.0 vs 25.4.
  This is all 1,296 copies against none; the share of the 751 new ones alone is not measured.
- No new errors in the log (the same three "file not found" lines as k255).
- The console was given back the k255 baseline afterwards (deploy 20261108_0926_e862fb0b41fd).
