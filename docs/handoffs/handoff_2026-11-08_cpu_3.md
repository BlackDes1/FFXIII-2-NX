# Handoff 3: distributed CPU work, hour 3 (2026-11-08, 13:10-14:10 UTC)

## State
- Console: k257 baseline, unchanged.
- app\generated: register-diet codegen in progress (bisect step k264c: non_volatile_as_local ON, elide_lr_stores OFF).
  Pre-diet tree: app\generated_before_k264 / app\generated_zones_before_k264 (restore by copying back; configs
  *.before_k264). Last known-good Switch build: out\k263 (k263 = k257 + round-2 copies - broken copy + mutant fix).

## This hour
1. k264b (diet with both keys, audit clean incl. all four zones; no share_registers marks): Ryujinx handheld booted,
   reached the menus and the save load, 1,184 copies reached, 26,793 calls compared, 0 differences, then the
   process ended silently at ~140 s during the first "NOW LOADING" (zone load). No guest fault logged.
2. PC build of the same code (21 min): exits 5 s after start, right after the first hook call, no message. The PC
   build compiles the codegen self-checks (REX_R1_CHECK, REX_LR_WRITE_SAME_CHECK) the Switch build leaves out; the
   link-register check is the first suspect with elide_lr_stores.
3. Bisect started: k264c = non_volatile_as_local only (elide_lr_stores OFF): codegen + PC build + a 200 s PC run
   to the loading point; if it passes, the Switch build and a Ryujinx handheld run follow.
4. ObjectTable::LookupObject: every lookup takes the kernel's global critical region (a recursive mutex shared by
   all kernel objects); 5 % of lookups contend on the console. A lock-free read needs a stable table and a
   retain-if-alive on the object; ~0.5-1 % of the main thread. Not started (risk vs gain).

## Measured this hour
- Nothing new on the console. k264b emulator run: correctness of copies (0 differences); boot failure.

## Next
1. k264c result (PC, then Switch + Ryujinx). If it holds: console A/B vs k263 (main thread CPU % and fps, overlay).
2. If k264c also fails: restore the pre-diet tree, keep k263 as the candidate, and continue with the store-cards
   experiment (BANJO_GEN_STORE_CARDS=OFF, separate build folder app\out\switch-nocards).
