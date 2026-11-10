# Handoff 2: distributed CPU work, end of the 2-hour block (2026-11-08, 11:10-13:10 UTC)

(Handoff 1 was written at 11:57 UTC, earlier than its title says; this one closes the two hours.)

## State
- Console: k257 baseline (unchanged; Works\builds\2026-11-08_0935_ffxiii2_k257_baseline).
- Build tree: app\generated = the register-diet codegen (backup of the pre-diet tree: app\generated_before_k264,
  app\generated_zones_before_k264; configs: app\renut_config.toml.before_k264, app\config\renut_funcs.toml.before_k264).
- Running: k264b = k263 + register diet (fixed), codegen + post-codegen done, full Switch build, then a Ryujinx
  handheld run with the copy guard (out\ryujinx_runs\k264b_check).

## New since handoff 1
1. k264 (first diet build) crashed in Ryujinx 30 s after boot: invalid access in sub_82505458 (r3 = 0x47F2CE2C)
   called from sub_82505A68. Cause: my two share_registers marks (0x82505B90, 0x82A53108). Both are normal
   functions that read r30 / r17-r31 only on a path after `_CxxThrowException` (0x82C150C8), which never returns;
   read_before_write.py did not know that and reported them. Marking a normal function share_registers elides its
   register saves and its callers copy clobbered ctx values back (the hazard the tool's own header describes).
   Fix: no marks; the audit gets the second noreturn seed (--noreturn=82C15900,82C150C8). Audit try3: only the four
   exact findings remain (xor r30,r30,r30 x2, a carry independent of r29, setjmp). Zone z0070 audited: clean; the
   other three zones are audited in the k264b job on app\generated_zones.
2. Diet effect on the generated code (static, k264): __savegprlr calls 13,514 -> 18; ctx.r1x-r3x references
   1,734,106 -> 962,599; ctx.lr writes 225,923 -> 2; NRO 93.6 -> 90.9 MB.
3. Store cards cost (static + profile): 231,476 card stores in the text (1.4 % of all instructions); on the main
   thread 1.0 % of samples sit on a card store and 1.3 % just after one. The render thread saves ~25 MB/s of
   hashing with them. Experiment planned: BANJO_GEN_STORE_CARDS=OFF (the renderer reads g_banjo_store_cards and then
   hashes; safe).
4. Main thread wait analysis (k260 sampled run, 21 % of its samples in the kernel): 52 % in a frame sync
   (NtWaitForSingleObjectEx from copy_82B69618), 13 % a guest Sleep(0) poll (sub_82B69868 = SleepEx), 9 % mutant
   releases with a wake syscall nobody needed (fixed in k263), the rest locks / event sets / render-queue notify.

## Measured
- k263 (mutant fix, broken copy out) in Ryujinx handheld: boot to the first battle, 860 copies reached,
  34,672 calls compared, 0 differences. Ryujinx fps does not reflect the A57 (emulator-bound): no speed claim.
- No console measurement of k262-k264 yet.

## Next
1. k264b: emulator check (boot, battle, guard 0 differences), then console A/B vs k263 in the same scene (two sessions
   each, overlay on, main-thread CPU % and fps of the steady blocks).
2. k265: k264b + BANJO_GEN_STORE_CARDS=OFF, console A/B.
3. non_argument_as_local audit (raw codegen app\renut_manifest_raw.toml / tools\ffxiii2_codegen_raw.bat, then
   tools/non_argument_audit.py with --noreturn=82C15900,82C150C8).
