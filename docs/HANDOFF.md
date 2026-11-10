# ffxiii2-nx handoff (2026-11-05, ~23:20 local)

## Update 2026-11-07 10:20 (local) - k241: defect class C2 closed on the PC
- User (~09:45): k240 on the Switch "better but not fully fixed"; wants a good PC build first, then each bug.
- Tree: Works\ffxiii2-k (no git). Files changed (backups *.before_k241 beside them; resource.cpp change additive):
  d3d/hooks/state.cpp (keys `ffxiii2_renut_stable_all_surfaces` default ON, `ffxiii2_diag_late_headers`; R4: copies
  keyed per header + contents; TakeSurfaceCopies), d3d/hooks/resource.cpp (copies released at the header's destroy),
  render/draw.cpp (validator calls in GatherTargets + resolve), backend/render_thread.cpp (`ffxiii2_diag_render_lag_us`),
  ffxiii2/ffxiii2_renut_glue.cpp ("[ffxiii2] headers:" counters line), tools/visual_debug/seam_pgm.py (new).
- **CONFIRMED (PC):** with k240's rules 6CB07E10 (post chain, 320x180 bound / 640x360 read) was still read late:
  270 / 10 s normal, 1,647 / 10 s with lag 1500 us. stable_all: 0 late reads, exits 0, battle 0 flicker / 0 dark of
  1,500, seam 1.215 (k240 1.229). Lag key reproduces D-010's exit #4 on the PC (l1).
- 10:18 DONE k241c: PC exe rebuilt with the default on (intro: 0 late reads, exits 0, 2/600 = cuts, 0 dark); Switch
  renut.nro 10:18 (71,256,261 B) built, NOT deployed - asked the user.
- (was) job k241c = PC build with the new default + intro regression (validator on) + Switch build (not
  deployed). Next: read out/k241c_grep.txt, k241c_flicker_intro.txt; if clean, ask/tell the user (k241 for the Switch;
  deploy as current only on the user's word, like k240); then the k240a-style console run with k241 when the user is
  not playing (restore the build the user is on, not rc8).
- Open after this: texture headers (BindTextureLocked reads header->Format; device fetch wins on different data - count
  [flash] lines), D-003 re-measure with k241 (the tiled scene's live colour headers were part of C2), D-006, shadow atlas.

## Update 2026-11-07 09:30 (local) - user: "Upload the fix"
- 09:29 k240 deployed to the SD as the **current** build (rc8 toml, cache rc9) at the user's request. From now on
  every test run restores **k240**, not rc8 (rc8 stays in Works\builds for rollback). A reminder scheduled earlier
  says "rc8 back" - ignore that part.
- PC k240 (pure-Python flicker_pgm.py, not comparable 1:1 with k238's PIL-based flicker_offline.py): intro 1/600,
  battle 4/1500 frames over 0.15 (max 0.26), 0 dark drop-outs, draw exits 0. Check the flagged frames (intro 4487,
  battle 10011 / 9022 / 9127 / 9132) before calling it clean.

## Update 2026-11-07 09:25 (local) - D-010 root cause and fix (k240)
- Tree: Works\ffxiii2-k (no git). PC exe being rebuilt (k240e running at the time of writing); Switch NRO
  app\out\switch\renut.nro = k240 (built 09:17, state.cpp recompiled). SD: rc8 (untouched since 08:48; the console
  stopped answering at 09:17 - botbase and FTP time out; probably asleep). Disk: > 30 GB free (build check passed).
- **Finding (CONFIRMED):** the Switch intro flicker on rc8 is not the game redrawing an earlier shot (that D-009 note is
  SUPERSEDED). k239u (normal speed): game-side draw sets monotonic; in ~20 of 2,599 renderer frames the post chain's
  composite draws (colour header 6CB07E60, view 14000500, EDRAM 0x2D0) are dropped by DrawStateLocked exit #4
  (GatherTargets: the header already rewritten to the next frame's depth view 0A010280) while their resolves run -> the
  frame shows the 3D scene black with only the subtitle layer. Game-side chains complete in those swaps.
- **Fix (k240, CONFIRMED in code, NOT YET VERIFIED on hardware):** `ffxiii2_renut_stable_dual_role_surfaces` (default
  true; d3d/hooks/state.cpp Ffxiii2StableSurface): a header the game has bound with a depth format is a bind-time copy
  in colour slots too. Backup state.cpp.before_k240. Applies to PC and Switch.
- Files changed: app/src/renut_engine/gpu/d3d/hooks/state.cpp; docs/diagnostics/BUGS.md (D-010 section, D-009 note);
  docs/progress-log.md; new read-only tools in tools/visual_debug/: chain_timeline.py, stale_vs_frontbuffer.py,
  orphan_resolves.py, missing_draw_chains.py, log_context.py, slice_between.py, lines.py, grep_logs.py.
- Results files: out/k239u/{timeline,orphans,missing_chains,nonrt_lines,slice_4255}.txt, out/k239t/{stale_vs_front,
  stale_vs_front2,orphans,black_rows}.txt.
- Next, exactly: (1) read k240e (pc_build_k240.log; k240_intro / k240_battle band dumps in app\out\pc\capturas\band\
  all_k240_*; the run logs' "[ffxiii2] draw exits" line: #4 must be 0; "colour binding of 6CB07E60 ... bound as a
  copy" line present); (2) when the console answers: rerun k240a (job lines in progress-log / this file: deploy
  renut_intro_chains.toml, home_launch, intro_ffxiii2.py --out out\k240a, get draw_chains/pass_counts, collect
  k240a_chains, rc8 back) and check `orphan_resolves.py` histogram (no n2D0 <= 3) and draw exits #4 = 0; (3) a
  read-back run (renut_intro_video_rt.toml) for black frames; (4) package rc9 with notes + credits, tell the user.
- Blockers: console unreachable (needs the user to wake it if it stays so). Frame Tap still excludes the game.

## Update 2026-11-06 12:30
- 120-min reports: docs/handoffs/2026-11-06_0955.md and 2026-11-06_1230.md.
- **D-007 fixed (k211, default on):** FF rewrites its scene surface headers in place for the shadow cascades and the
  bloom chain; `ffxiii2_renut_stable_rewritten_surfaces` binds a copy of a header whose height changed since its first
  binding (the console's D3D reads headers at bind time). Copies on tiling / format-only rebinds split the scene
  (k207-k209 seam at x 640): only another height is copied. User granted "the privilage to fix every bug".
- D-003 band still open: ruled out the delta chain (verify 0 differ), the cascades, stale strips, depth hand-over.
  k212 key `ffxiii2_deko3d_tiled_cache` (diagnostic, -1 default): alternating A/B running (logs/visual/k212/ab).
- Procedures: recordings start only after nav reports the battle (D-006: deaths after a stream attached during load);
  HOME asks for a user after a reboot (home_launch handles it); nav retries pixelPeek. Diagnostic keys k206:
  `ffxiii2_diag_bind_log_start_s` / `_count` ([bindlog]).
- Console runs experimental k212 with renut_frametap_*.toml (visual sessions, uncompressed swapchain).

## Update 2026-11-06 08:55
- 120-min report 08:05: docs/handoffs/2026-11-06_0805.md. Frame Tap fully working (USB viewer, headless recordings,
  PNG with `ffxiii2_swapchain_compression = false`). Ghost inputs fixed (our tooling). NAND backup not needed (user).
- Smoke band (D-003): the old metric was wrong; band_scan.py on recordings: base 3-5 % of frames, F1 2.0 %, G 4.8 %
  (all table renderer enhancements off): not the enhancements. Frame-stamped k203 + [rtlog]: banded frames have the
  same pass/surface sequence as clean ones. Running: GC (CPU enhancements off too).
- Tool safety: nav_ffxiii2 presses only while FF XIII-2 runs; home_launch accepts only FF XIII-2 starting and stops
  after 6 screens without the tile row; botbase.py tolerates a late "done" in pixelPeek replies.
- Console runs experimental k204 (diagnostic keys off by default; tomls renut_frametap_*.toml for visual sessions).

## Update 2026-11-06 05:50
- **Switch Frame Tap 0.7.21.1 installed on the SD** (additional visual-debugging capability; docs/SWITCH_FRAME_TAP.md).
  27 files SHA-256 verified; config.ini written (memory_boost 0, homebrew_docked 0, 720, screenshots ZL+ZR+D-pad Down,
  usb). SD config backup `Works\switch_sd_backup\20261106_053943` (565 files). Starts after a reboot (the user's).
- Asked the user: reboot, NAND backup (none on the SD; emuMMC present), USB WinUSB driver (Zadig) or network firewall
  prompt for the PC viewer. Side runner waits up to 60 min for Frame Tap's log.txt, then fetches it to
  logs/frametap/first_boot.
- Tools: `tools/switch_remote/frametap_sd.py`, `tools/visual_debug/vd.py`. Perf sessions need Frame Tap off.
- Flicker k197d: F1 1/7 banded, base 9/30: too few F1 battle screens; rerun longer.

## Update 2026-11-06 04:40
- **Audio fixed in k196** (sdk/src/system/xtimer.cpp: absolute due time 0 / past = now; the game's 16 ms sound timer
  never fired on the Switch). Console log: stream Start, XMA submit, 330 kicks / 10 s as on the PC. Deployed k196 with
  renut_dev.toml (experimental slot) for the user to listen. Details: docs-notes "Audio root cause".
- The job runner had stopped overnight; restarted from the side runner (Start-Process run_job.cmd). Both runners stop
  after 12 hours of watching: either one can restart the other with Start-Process.
- **User (2026-11-06 ~04:45): "I can hear the audio."** k197: the audio diagnostic is no longer built (file kept).
- Running: flicker test F1 (the 35 deko3d / sa2 keys outside the enhancement table off, `renut_flicker_f1.toml`)
  against a base run of the same build.

## Update 23:20
- Deployed on the SD: k186 NRO (experimental slot, audio diag compiled in, off unless ffxiii2_audio_diag), DKSH cache
  rc8 (505 MB). rc9 built (`out\deko3d_shaders\deko3d_shaders_rc9.bin`), not deployed. Saves backed up again
  (`switch_saves_backup\20261105_214357`, identical to 19:07 by SHA-256).
- Bottlenecks: run-time compiles fixed (38 -> 14 background, 0 blocking); guard off by default (k165). GPU: resolves
  2.4 ms + shadow depth resolve 1.45 ms + smoke 3-4 ms are real (serialised profile); the copies can only go with a
  coverage proof (not done). Present runs 6-11 ms above the GPU: frames miss the 33.3 ms vblank pair.
- Audio: see docs/docs-notes.md "Audio, narrowed" (the stream source voices are never started on the console; the
  engine's operation queue is empty there). Next: the game's enqueue of those Starts.
- Flicker: not one of the 64 table keys (G2 27%, base 25%); left: deko3d keys outside the table / base tiling.
- Seen in several runs: a long white battle screen with the HUD up (game running). User (2026-11-06): ignore for now,
  remember for later (docs-notes "Deferred").
- User (2026-11-06): `banjo_gpu_boost_ram_1600` removed (k187; its toml moved to `ffxiii2-k\_to_delete\`).

## Update 20:50: live bottleneck hunt (handheld only: the user, 20:20)
- Remote loop verified end to end many times: deploy (NRO 50 s), HOME tile start, nav_ffxiii2.py to the battle
  (85-130 s), overlay L3+R3, collect, kill. Incremental Switch builds: 1 min (k161-k163).
- Deployed: k163 NRO `..._7a7e09381021` (experimental slot), toml varies per test; SD cache still rc4 + console adds.
- Measured (battle, handheld, apm GPU 460.8 / EMC 1331.2): GPU 28-36 ms, Present 37-40 ms, 21-28 fps. Resolves and
  depth resolves about 6.5 ms of GPU a frame (serialised profile confirms the spans); smoke PS FCF76CCF 3-3.9 ms.
  RAM 1600 (banjo_gpu_boost_ram_1600): no gain. Depth compression off: worse. Main thread (stack sampler): guest code
  about half, waits the rest (15% on the GPU fence in the battle); the hot-copy guard cost about 5% of it -> k165
  default banjo_hot_guard_calls/period = 0 (0 differences in 21,209 console compares).
- Overlay fixed (k161): draws a frame / stutter use Present when Application is n/a.
- New diag keys (off): renut_gpu_profile_serial, ffxiii2_audio_diag (callers of voice create sub_82B90650 and
  submit sub_82B8E950; src/ffxiii2/ffxiii2_audio_diag.cpp, not yet built), per-size resolve labels with
  banjo_renut_diag_gpu_shaders, depth hand-over refusal log.
- Running: k164 = DKSH cache rc7 (rc6 inputs + the console's pipeline list/cache, `out\console_k163`). Then deploy
  rc7 and build k165 (guard off default + audio diag).
- Seen in the battle (k163_noguard nav run): a long white/pink full-screen scene with the HUD up, game running
  (24-30 fps, GPU 12-16 ms). Not judged from the picture: compare with the PC / the log before calling it a bug.

## Remote testing environment (docs/LIVE_SWITCH_TESTING.md)
- sys-botbase: installed, running (`2.6-files-kill`). sys-ftpd-light: installed, login/upload verified (slow: 510 KB
  in 56 s; botbase is the transport).
- Connection: working from the PC (side runner `Works\ffxiii2-nx\run_side.cmd` -> `out\side.txt`). IP <console IP>.
- Deployed build: **k157 NRO** `20261105_1701_081c0c675420` (overlay, perfil_pilas.flag, counters), toml
  `renut_dev_k157_cpuoff` (audio A/B, deploy `20261105_1747_...`, slot experimental). rc4 in SD
  `deploy/known_good/manual_20261105_1701/`.
- Last successful deployment: 17:47 (toml only; NRO 17:01-17:11, 69.2 MB in 515 s with SHA-256 read-back).
- Latest logs: `logs\switch\20261105_1747_081c0c675420\audio_cpuoff\` (renut_019).
- Profiler: perfil_pilas.flag verified on hardware (k157), removed again (off). Overlay L3+R3 verified.
- Remote start: `home_launch.py` (tile picture match) verified 3x. Inputs reach HOME but **not the game** until
  k158 (`input_shared_players`). Game logs are **locked while running** until k158 (`log_file_reopen`).
- Hardware-side open: none (the user allowed full remote operation).

## Audio (XMA never kicked on the console) - narrowed 2026-11-05
- Console renut_017/019: register 0x601 written ~375/s (voice loop sub_82B92708 runs), XMACreateContext + clear,
  **kick 0**; PC aud2: same, kick 13 ms after create on the same thread.
- **Not the CPU enhancements**: banjo_cpu_enhancements = false on the console (k157, "cpu master OFF", no [hot]) still
  kick 0.
- Mechanism (from the recompiled code): voice table 0x82EB2058 (320 voice ptrs); voice +0 count, +4 flags, +8 contexts
  (96 B each: +64 hardware pointer, +80 index). Creation sub_82B92B80 sets flags |= 0x20000; submit sub_82B92878
  (vtable slot 69, byte 276, called through 18 virtual call sites) copies each context to the hardware pointer,
  dcbf, kicks 0x650, clears 0x20000; the loop kicks only voices with 0x4 set and 0x20000 clear.
- Next: tools/switch_remote/xma_voices.py on k158 (live log -> guest base -> peek the table) to see the voice
  flags on the console vs the PC.

## Running
- Job k158 (17:00): XenosRecomp with BANJO_ALLOW_SHADER_FAILURES -> switch_shaders -Scan: 24,921 shaders, 24,920
  compiled to DKSH (279 vs, 24,641 ps; 36 DXIL failures left out, 1 SPIRV-Cross failure ps_06965BC98D960B6E), then
  Switch build (input_shared_players, log_file_reopen), cache rc6, Ryujinx r21.
- (done) Job k157 (started 16:17 PC time): selftest PASS on Windows, sysmodules installed, ffxiii2.toml = renut.toml
  (flicker test C in before_k157), rest of the scan, switch_shaders -Scan (complete set), Switch build, cache rc5, Ryujinx r20.
- Lost with the old cloud container (re-done 2026-11-05 15:10): switch_shaders.ps1 -Scan, renut_switch_shader_cache.py
  --cpp/--all. Progress-log rows after 10:40 UTC exist only as CLAUDE.md's summary.

## Flicker (none of tests A/B/C stops it: user, 2026-11-05)
- Video 2026110513383000_s.mp4 (Bahamut battle): the bad frames lose the smoke layer **above y ~ 336** (the sky with
  floating debris shows instead), everything else right. 336 = the height of a predicated tile for 1280x720 with a
  64-bit colour target + depth at 2x MSAA in 10 MB EDRAM (1280 x 336 x 24 B). So: the first tile's pass of the smoke
  (alpha particles) is missing in some frames. Candidates: memexport-fed particle vertices not ready for the first
  tile's replay ("Draw waits for memexport results ... GPU round trip" for VS 6549C96C96C2721F), or a draw dropped
  per tile. Next: count dropped draws per tile (g_ffxiii2_draw_exit) in a console run; Ryujinx r-run of the battle.

# (previous handoff, 2026-11-05 ~09:20 UTC)

## Where things are
- Cloud `/home/claude/ffxiii2-k` (git `master`, source of truth, not pushed: push access to `BlackDes1/FFXIII-2-NX` was
  refused by the session's permission check; needs the user's grant). PC `Port\Works\ffxiii2-k` (no git; synced by
  `Works\_xfer\k_syncNNN.tar`). Jobs: `Works\ffxiii2-nx\run_job.cmd` reads `out\job.txt` (single quotes only, commit
  from a uniquely named staged file; double-click at (156, 398) in the `ffxiii2-nx` Explorer window).
- Order in force: 2026-11-05 "Xbox 360-accurate PC port, then Switch". Docs: `docs/verification-matrix.md`,
  `docs/reference-catalog.md`, `docs/docs-notes.md`, `hand.txt`, `docs/progress-log.md`.

## Build / package state
- PC exe (08:33 build): 209 copies + polling backoff; verify4 0 differences.
- Switch NRO (k143, 09:35): + `banjo_deko3d_push_constants = false` built in, 37 console shaders; 65,771,717 bytes.
- **rc2** `Works\builds\2026-11-05_0947_ffxiii2_rc2` (DKSH cache 1,919 variants, source 5aa81c478b9f, notes
  `docs/package_notes/rc2.txt`). Not yet on the SD card.

## Xbox 360 reference
- xenia session ref2: `out\xenia_ref\ref2` (58 captures, `norm\` 1280x720, `contact.jpg`); `tools\xenia_ref.ps1`
  (keys: A Enter, B N, Start X, Back Z, D-pad arrows; held 0.15 s; `--keyboard_mode=1`). Drive by computer use
  (request "xenia_canary.exe" once it runs; maximise the window).

## Verified vs xenia (secondary evidence)
- PASS: title/main menu text, "Begin game" dialog, battle tutorial brightness and HUD layout.
- Not a bug: the FMV dot grid (the game's pause screen).

## Open, in priority order
1. PC vs 360: DOWNLOADABLE CONTENT enabled on the PC, greyed on xenia (ruled out: sign-in state, privileges, DLC
   enumeration). Next: trace XAM/XNet calls on the title screen.
2. PC screens not yet compared: Settings, Beginner's Primer, in-game Primer, Extras, opening cutscene, subtitles. A
   hand-driven PC run (the game window by computer use) or state-waiting script; refpc1's wall-clock script drifted.
3. Switch: dialog panels teal (PC/xenia dark blue); the in-game Primer picture garbage; push_constants' own bug.
4. Console rc2 test by the user (UI, backoff A/B target 19 + control, profile).
5. Double codegen per PC build (generator relinked).

## Next action
Copy rc2 to the SD when the user wants it; hand-driven PC reference run of the same path as ref2 (Settings, Beginner's
Primer, in-game Primer), compare screen by screen; XAM trace for the DLC item.
