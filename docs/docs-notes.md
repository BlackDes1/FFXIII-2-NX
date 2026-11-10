# Final Fantasy XIII-2 port: technical notes (docs-notes.md)

The running record of decisions and discoveries. Details and history: `docs/ffxiii2-nx.md` (the long notes),
`docs/progress-log.md` (dated), `docs/verification-matrix.md` (Xbox 360 vs PC vs Switch).

## Paths (verified 2026-11-05)
- Working tree: `Port\Works\ffxiii2-k` (PC; no git there) and the cloud copy `/home/claude/ffxiii2-k` (local git,
  branch `master`, the source of truth; synced to the PC by tar + MD5-checked copies). `Port\Works\ffxiii2-nx` is the
  older first tree, kept; its `out\` holds the job runner (`run_job.cmd`, `out\job.txt`, `out\job_*.log`).
- Base: `Port\katamari-nx-main`; reNut: `Port\renut-nx-master`. Game data: `Works\game_root` (disc + TU6 xexp),
  `Works\game_data\tu`. Emulators: `Port\ryujinx\Ryujinx.exe` (Switch), `Port\xenia_canary.exe` (Xbox 360 reference).
- Git remote: the user's private `BlackDes1/FFXIII-2-NX`; attaching it with push access was refused by the session's
  permission check (2026-11-05): no push yet.

## Game
- XDK (default.xex static-library list, header 0x000200FF, read 2026-11-05): **2.0.20871, highest QFE 2** (XAUDIO2,
  XAPOBA); compiler/linker 16.0.11886 / 10.0.11886; title ID 53510806, base version 0.0.0.11 (TU6 0.0.6.11). The same
  XDK build as Zuma's Revenge (2.0.20871), whose Direct3D hook map this tree's renderer hooks came from.
- EU disc, English, title update 6 (default.xexp 0.0.6.11); zone modules z0070/z0130/z0160/z0161 linked statically on
  the Switch. Shaders live mostly in the game's data files (not the executable): the shader package comes from what PC
  runs and console runs captured.

## Codegen / generated-code fixes
- Post-codegen pipeline: `tools/post_codegen.py` (many steps; the last ones: store cards, then
  `tools/spin_polls.py`, 2026-11-05).
- `tools/spin_polls.py` + `src/ffxiii2/ffxiii2_spin_poll.cpp`: a backoff in the 22 pure guest polling loops (one load
  from an address the loop does not change, compares, the back branch): CPU yield hint 64 polls, then core yields,
  then 25 us sleeps. Same values and order of guest accesses. Key `ffxiii2_spin_backoff`, A/B target 19. Reason: the
  rc1 console profile (effect job system's barrier/election waits: four workers at ~240 % CPU, main thread 14 %).
  verify4 (PC): 209/209 copies, 0 differences.
- Machine copies: 209 (`app/src/cpu/hot/localized_bulk.txt`), verified (verify3/verify4: 0 differences).

## Renderer (Renut) decisions
- Predicated tiling (2 tiles 640x720): BeginTiling `sub_8285CC20` / EndTiling `sub_8285CF90` hooked pass-through;
  tiled resolves offset to their tile. Gamma setters `sub_8284C198` / `sub_8284C268` pass-through (identity ramp).
- memexport on deko3d: stores and packing with constant vector components (uam dropped components of run-time
  indexed vectors). `vfetchBound` has the same pattern but FF never calls it.
- **deko3d: banjo-nx's enhancement keys break FF's UI** (2026-11-05): with all 20 default-on `banjo_deko3d_*` keys
  off the title, dialog and save screen render right in Ryujinx; the culprit is in {push_constants, draw_resolves,
  depth_handover, depth24, depth_compression}. To be narrowed, root-caused and fixed (or the key off for FF with
  the reason) before any other Switch feature work.

## Enhancement rules checked
- renut-nx `porting-guide-renderer.md` 6b (slower on the console): replay_lookahead, shadow_sort, texture_slot_skip are
  not in this tree; `sa2_renut_inline_line_compare` set off in `ffxiii2_switch_defaults.toml` (inert while
  push_constants is off).

## Build pitfalls
- The build's own codegen step rewrites `renut_pch.h` after ninja statted it: the job scripts touch cmake_pch.hxx
  when the header is newer than the PCH and retry once on a stale-PCH failure.
- The PC build relinks the generator and re-runs codegen itself after a job's explicit codegen (~16 min twice).
- The job runner strips double quotes; `device_commit_files` may reuse an earlier upload of the same staged path.

## Switch
- deko3d only, no NVK. Own process handle via the IPC trick (Ryujinx/Eden refuse the pseudo-handle).
- Console rc1: 6-7 fps in the field; run-time shader variant compiles stall seconds (the shipped DKSH cache is small);
  37 shaders missing from the package (captured on the console: `out/console_rc1/shader_capture`).

## Live console findings (2026-11-05 evening, handheld only)
- Handheld clocks per the apm log: CPU 1020, GPU 460.8 (0x92220008), EMC 1331.2. EMC 1600 (0x92220007) measured no
  gain; the test key `banjo_gpu_boost_ram_1600` was removed (user, 2026-11-06; k187).
- **Deferred (user, 2026-11-06: ignore for now, remember for later):** in several automated battle runs the screen
  turns almost fully white (grey silhouettes) with the HUD up for minutes while the game keeps running (logs clean,
  saves unchanged). Runs: k163_noguard, k165_rc7, k168_flkH1 (`logs\switch\...\nav\`). Not judged as a bug yet.
- Battle GPU frame 28-36 ms. Colour resolves 1280x720 RGBA8 about 0.25 ms each (~2.4 ms a frame), shadow depth resolve
  (1280x1024 surface, 1024 rect, no words) 1.45 ms, tile depth resolves with words 1.0 ms; the serialised profile
  (`renut_gpu_profile_serial`) gives the same spans, so they are real. Depth hand-over never applies (source wider
  than the resolve; tile halves). Removing the copies needs a proof that a pass covers its target: not done.
- Hot-copy guard: about 5% of the main thread (range sorting, double runs); 0 differences in 21,209 console compares
  -> off by default from k165 (`banjo_hot_guard_calls/period = 0` in ffxiii2_switch_defaults.toml).
- DKSH cache: the generic variants missed every discarding pixel shader's early-Z variant (909) and its alpha
  functions (903): rc7 still compiled 38 at run time. `tools/deko3d_variant_cache.py --generic` adds them (rc8).
- Audio: on the PC the voice submit (sub_82B8E950) is called every 5-10 ms from sub_82BD4DA0 (lr 82BD4FA8; stream
  object vtable 821F644C, its +268 the XMA object, vtable 821D7ED8 slot 7). `tools/switch_remote/xma_objects.py`
  looks for the same objects on the console.
- Audio, narrowed (22:40): guest call chain on the PC (aud8): submit <- sub_82BD4DA0 (stream object slot 0) <-
  sub_82BB9C80 (walks the engine's voice list) <- sub_82BBAFE8 (the engine pass: manager 0x4000D6C0, vtable 821DCFDC,
  same address on both machines) <- sub_82BBB1B0 <- sub_82BB9BF0 <- thread sub_82B6B840. The engine's voice list
  (manager +592; `tools/switch_remote/guest_list.py --head 0x4000D910`): PC 22 entries, the first the stream object
  (vtable 821F644C) and two voices (821F6414) with it; console 19 entries, those three missing. The stream object
  exists on the console (its XMA object and voice too), but its voices were never put in the engine's list: the
  start / graph commit of that source voice does not happen on the Switch. Next: who inserts into +592.
- Audio, narrowed further (22:55): the active list is rebuilt by sub_82BBAD80 from the all-voices list (+372) with
  the voices whose slot 2 (sub_82BD2AB8: [+100] == 0) says active. The console's stream source voice is in +372 with
  +100 = 1: never started. Start = slot 5 (sub_82BD3E88: +112 = 0, +100 = 0, the XMA object's slot 0). On the PC
  (aud12) Start runs from the engine pass's operation queue (sub_82BBD1E0 -> sub_82BBD060 over [manager+152]+212)
  for five source voices in the first second; on the console (k180, renut_audiodiag.toml) Start is never called and
  that queue is empty: the game never asks to start its streams. Next: the game side that calls the public
  Start (IXAudio2SourceVoice::Start, queued) and what it waits for (stream data, XMA readiness).
- k185 (23:10), both machines with ffxiii2_audio_diag: sub_82BBC628 (slot 17 of the public voice vtable 821DD080;
  NOT Start: called from the game thread through sub_82BBD1C0 for voices 40013A04 / 40013A34, chain 82BBD02C <-
  82BBD1C0 <- 82B9DB10 <- 82B91A64 <- 82B87A0C <- 82A2EBDC (game) ...) runs the same on the PC and the console. The
  internal voice Start (sub_82BD3E88) runs 12 times on the PC (stream source voices, from the engine pass's operation
  queue) and 0 times on the console. Next: map the public vtable 821DD080 properly and find the game's real Start
  call for the stream voices (vtable 821F644C) and why the console never makes it.
- 2026-11-06 (k188/k189, PC aud15/aud16 vs console): operations reach the queue only through sub_82B9CE30, from two
  virtual methods: sub_82B9E600 ("B", the game thread: voice creation and outputs) and sub_82B9DB38 ("A", the engine
  pass applying operations). B #0-#26 and A #0-#21 are the same on both machines (same objects; #26 = the music
  stream's source voice 4009C0E0, created by the stream's Init sub_82B90848 = slot 30, which returns 0 on both). The
  one difference: A #21 (the operation of the public voice 4001732C) has r7 = FFFF0002 on the PC and FFFF0001 on the
  console. On the PC the stream voice's Start follows at once (same pass), then the game thread goes on (B #27-#34,
  three more stream voices, Init #1-#3, Starts #1-#4). On the console: no Start, no B #27, no Init #1. Next: where
  the FFFF000x word comes from (the A #21 operation's data) and what decides 1 vs 2. (That word turned out to be
  leftover register contents; not the cause.)
- **Audio root cause (2026-11-06, k196): the game's 16 ms sound timer never fired on the Switch.** sub_82BDC3F8 (a
  thread) arms a periodic timer (NtCreateTimer, NtSetTimerEx with the absolute due time 0 and period 16 ms) and runs
  sub_82A60E80 -> sub_82A5FF00 -> slot 10 of [0x82EB0F3C] (sub_82C09E88) on every tick; that tick drives the
  stream's play (sub_82B8CB48), the first source buffer (command kind 0), the voice Start and from then on the XMA
  submit and kicks. The SDK's XTimer converted the year-1601 FILETIME through clock casts to the steady clock: due at
  once on Windows, never on the Switch. Fix: sdk/src/system/xtimer.cpp, absolute due time 0 or any past absolute time
  = now (the Xbox 360's behaviour). Console: ticks every 16 ms, voice Start, submits, 330 kicks in 10 s (PC 329). The
  diagnostic (ffxiii2_audio_diag, off) stays for now.
- Flicker band (y 336), battle screens only (`flicker_metric.py` default now): base 25%, zcull off (D) 33%, all 64
  table renderer keys off (G2) 27%, half of them off (H2) 26%: none of the table keys causes it (the first G run's 10%
  was cutscene shots). Left: the banjo_deko3d_* keys outside the table, and the base deko3d / tiling path.
- 2026-11-06: F1 (the 35 deko3d_* / sa2_renut_* keys outside the table off) 18 of 66 battle screens banded (27%)
  over three runs; base 36 of 178 (20%) over four: **those keys do not cause it either.** (F1 runs ended in the long
  white scene in 2 of 4 navigations, base 0 of 4: noted, the white scene stays deferred.)
- k198 `[tilelog]` (ffxiii2_diag_tile_ps = FCF76CCFFD55FFDE, draw.cpp TileLog, 900 battle frames): FF tiles the
  battle **left / right** ([0,0-640,720] [640,0-1280,720]), not top / bottom, and every smoke draw runs in both tiles
  (predication FFFFFFFF, scissor 0-720 rows, full-screen viewport); its draw count grows smoothly with the effect
  (60 -> 250 a frame) with no drop-out pattern near the banded share. So **y 336 is not a tile edge and no smoke draw
  is lost or cut**: the missing upper part comes from inside the draws - what the smoke pixel shader reads (soft-
  particle depth, a resolved texture) or the target it blends into. Next: the smoke draws' bound textures and target
  (drawlog for one banded and one clean frame).

## Known limitations
- No Xbox 360 hardware reference; xenia captures are secondary evidence.
