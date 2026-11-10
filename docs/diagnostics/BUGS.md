# Bugs and findings (diagnostic register)

Classes: BUG_CONFIRMED, RENDERING_CONFIRMED, PERFORMANCE_CONFIRMED, STABILITY_CONFIRMED, REGRESSION_CONFIRMED,
SUSPECTED, NOT_REPRODUCED, FALSE_POSITIVE, RESOLVED_VERIFIED. Older findings: docs/docs-notes.md and progress-log.md.

## D-001 Ghost inputs on the console after the Frame Tap install (user report 2026-11-06 ~07:25)
- Class: RESOLVED_VERIFIED (user, 2026-11-06 ~07:35: "No Ghost inputs" after the detach and with no automation
  running). Cause: our tooling (below), not Frame Tap.
- Evidence: Frame Tap's own logs (log.txt / log.prev.txt of both boots, logs/frametap/*, logs/visual/k200/*) have no
  `vpad` line: with `viewer_input = 0` its code (applet_mitm_vpad.cpp) never opens hid:dbg; its input thread only reads
  the pads (`padInitializeAny`, read-only) for the screenshot combo and HOME. What did send input in that time: our
  automation, while the user was holding the console: home_launch (HOME, D-pad, A), nav_ffxiii2 (A, START...), vd.py
  shoot (ZL+ZR+D-pad Down 2-12 times a session; it moved the battle menu cursor), between 06:44 and 07:23. And
  `botbase.py` sent `setStick LEFT/RIGHT 0 0` when closing **every** connection (file transfers, status, screenshots),
  which makes sys-botbase attach its virtual Pro Controller; nothing ever detached it.
- Fix (tools only): botbase.py releases / centres only on connections that sent input; new `switch.py detach`
  (release all, centre, detachController), run 07:27; every automated test ends with it from now on. No automated
  input while the user handles the console. selftest.py: ALL PASSED after the change.
- Open: the user to confirm whether ghost inputs remain with the controller detached and no automation running.

## D-002 Frame Tap screenshots of the game are black
- Class: RESOLVED_VERIFIED (capture side; the game's picture was right).
- Evidence: k199 PNGs exactly (0,0,0) while sys-botbase's JPEG of the same moment showed the title; Frame Tap log:
  game swapchain 1280x720 found, VIC copy done. Our deko3d swapchain images had DkImageFlags_HwCompression
  (plume_deko3d_commands.cpp); the VIC reads the raw memory.
- Fix: key `ffxiii2_swapchain_compression` (default true = unchanged); renut_frametap.toml sets false. k200: battle PNGs
  with the picture (means 32-47 / 45-68 / 46-72), matching sys-botbase's capture. Lossless setting: same pixels.

## D-003 Smoke flicker band (smoke missing above y ~336 in 20-30 % of battle frames)
- Class: RENDERING_CONFIRMED (rare on current builds), cause open.
- **2026-11-07 k244e (Switch, k243d renderer = k241 header latching + SA-1, Frame Tap USB, every game frame):** Chaos
  Bahamut battle from save 20, 2,062 battle frames (then GAME OVER, nobody playing): **0 banded** (edge share max
  0.40 / 0.39 in two single frames, by eye normal; banded frames measure 0.50-0.54). Earlier recordings: 0.07-4.8 %
  with clusters, so 69 s is not yet proof; candidate cause fixed: the C2 class (headers read late, D-010 / k241),
  which the Switch's slower render thread hit far more than the PC. Next: a longer recording (the battle played, not
  idle) before closing. Test tooling of this run: battle_record.ps1 -FromLoadScreen, nav --phase, input_test_merge_pads
  (virtual controller is player 2 while the handheld Joy-Cons are attached), compare_save_backups.py (saves unchanged).
- 2026-11-07 k240, WEAK HYPOTHESIS (unverified): the band is first seen in resolve 6D05869C, which in the [rtlog] is
  one of the post-chain resolves fed by draws on colour header 6CB07E60 (EDRAM 0x2D0) - the header whose late read
  dropped those draws (D-010) and flipped its host-surface key between the colour and depth targets (UnuseTarget can
  retire a target). Re-measure the Switch band with a k240 build before any other D-003 work.
- **The metric was wrong (2026-11-06):** flicker_metric.py's above/below brightness test flagged 20-30 % of battle
  shots that have no edge at all (checked by eye and by an edge scan: k198's 16 "banded" shots have no edge near
  y 336). Every key A/B since 2026-11-05 (A/B/C, D, G, G2, H1/H2, F1) measured scene gradient, not the band: those
  conclusions are withdrawn. New default: the edge method (a downward step at y 330-344 in >= 37 % of the sampled
  columns). Calibrated on the user's video 2026110513383000_s.mp4 (10 fps): 15 of 97 frames banded, edge share
  0.50-0.54 at y 334-337; other frames <= 0.24.
- Re-scored: every console run since k163 together: 7 of 731 battle shots (1 %) banded, 4 of them in k197_base
  (80 shots, during the tutorial dialog). The user's video (build of 2026-11-05 13:38): 15 %. So the band still occurs,
  much rarer in these runs (different moments of the battle, different builds): a rate needs far more frames ->
  Frame Tap USB recordings (viewer --headless --mp4, 30 fps) and the edge metric per frame.
- Ruled out by logs (still valid): not a predicated tile ([tilelog]: the battle tiles left/right at x 640; every
  smoke draw full screen, predication FFFFFFFF). drawlog k200 (10 frames): the smoke (PS FCF76CCF) is drawn in two
  groups per frame, the first depth-tested against depth target 76 (dc 08708732: test LEQUAL, no write), the second
  with no depth target; where the switch falls varies frame to frame. A wrong depth content above y 336 would cut
  only the first group: the next check, against a banded frame.
- Recordings (Frame Tap USB, headless, every game frame; `tools/visual_debug/band_scan.py`): k200 92/3,059 (3.0 %),
  k201 101/3,061 (3.3 %), k202 24/3,135 (0.8 %, a later phase), k203 154/3,189 (4.8 %). The band row is fixed
  (y 334-337 in 95 % of banded frames, the user's video the same): not a tear.
- k203: frame stamp (`ffxiii2_diag_frame_stamp`, diagnostic) read in all 3,189 frames and matched to `[rtlog]`
  (3,000 frames, every render target / depth surface in draw order, resolves, clears): **banded and clean frames have
  the same surface sequence** (no token separates them at any offset -2..+2; best: the bloom ping-pong order, 0.53 vs
  0.33). So the band is not a different pass order or a missing resolve; it comes from inside the passes (GPU
  ordering / data). Next: the F1 keys again with a recording (their earlier "no effect" came from the wrong metric),
  then bisect.
- 2026-11-06 ~10:00, second shadow cascade (k203 [rtlog], 3,000 frames): the cascade-2 draws use the scene's depth
  header 6CB07E60 (640 pitch, 2x MSAA) instead of the cascade's 3018C000 in 1,240 + 723 frames, a mix in 342, right in
  585; the resolve after them always reads 3018C000 (so cascade 2's shadow map gets no draws in those frames: a
  separate SUSPECTED shadow bug). Against the band (stamps): banded 6.6 % of wrong-header frames, 4.8 % of right ones:
  **not the band's discriminator.**
- k205 run with `sa2_renut_snapshot_deltas_verify` (renut_098, 2.5 M battle draws): **0 differ** - the render thread's
  device equals the game thread's for every draw, so the delta chain is ruled out; the game-thread device really
  holds 6CB07E60 for those draws. k206 adds `[bindlog]` (every binding call, `ffxiii2_diag_bind_log_start_s`) to
  find the call that leaves it bound.

## D-007 Shadow cascades drawn into the scene's targets (found 2026-11-06 in the D-003 chase)
- Class: RESOLVED_VERIFIED (k211: key default on, under the user's "You have the privilage to fix every bug",
  2026-11-06 ~11:45; verified on k210 with the key on: [rtlog] both cascades right in 2,689/2,689 battle frames, 4 shots
  without seam). Changes the picture (correct shadows / bloom); `ffxiii2_renut_stable_rewritten_surfaces = false`
  brings back the old behaviour.
- Evidence: k206 `[bindlog]` (renut_099): FF rewrites its heap surface headers in place between bindings. 6CB07EB0
  (scene colour) and 6CB07E60 (scene depth) are rewritten as 1024x1024 cascade depth headers (info 10000410), bound,
  drawn into, and rewritten back to the scene's layout (0A010280) before the render thread reads the header for those
  draws. The console's D3D takes the registers at bind time; the renderer read the header later, so cascade 1's draws
  went nowhere (no depth target) and cascade 2's into the scene depth (k203 [rtlog]: right in only 585 of 3,000 battle
  frames). The same race hits the bloom chain headers (6CB07E60 / 6CB07E10 / 6CB07DC0 rewritten 1280x720 <-> 640x360
  <-> 320x180 within a frame).
- Fix (k210): key `ffxiii2_renut_stable_rewritten_surfaces` (default false): a heap header bound with another height
  than at its first binding is bound as a copy of its contents at bind time (as the stack headers already are).
  First versions (k207-k209) also copied on format-only and tiling rebinds (640x720 2x MSAA tiles): the renderer's
  tiling is built on reading those as the full screen, and the copies split the scene (seam at x 640 in 4 of 5
  shots) -> restricted to another height.
- Verified (k210, renut_103 [rtlog], 2,689 battle frames): both cascades drawn into their own surfaces in every frame;
  4 battle shots without seam. Open: Frame Tap recordings (band rate key on / off, needs the reboot), the user's
  look at the shadows, then the default.

- 2026-11-06 11:20, k210 band A/B (Frame Tap after the reboot): key ON 91 of 3,006 frames banded (3.0 %): **the
  cascade fix (D-007) does not remove the band.** Banded frame 690: above y ~336 the smoke columns are flat pale
  shapes, below it the scene is right (the user's "pale silhouettes"). Key OFF control: first try recorded the
  title (nav stopped on a failed pixelPeek: nav_ffxiii2.py now retries), second got 0 frames (Frame Tap excluded the
  game again, see D-006).
- 2026-11-06 12:15-12:55, k212 key `ffxiii2_deko3d_tiled_cache` (Maxwell tiled cache), alternating runs, one launch
  each: as before 1.86 % (54/2,907), 0.07 % (2/3,049); off 0.66 %, 0.13 %, 1.25 %, 1.16 %. **Not the tiled cache.**
  The rate depends on the battle moments recorded (banded frames come in clusters, e.g. 7 in a row), so random 150 s
  recordings cannot judge a key by its rate. No period of 3 frames (pairs within 30 frames, all recordings: 2,038 /
  2,134 / 1,891 by distance mod 3): not a 3-deep ring or swapchain image. Ryujinx is no reference (its battle renders
  broken, r22). xenia-canary (user, 12:40): commit / issue pages blocked for automated reading; its GPU flags
  (half_pixel_offset, xenos_sample_positions) do not explain a fixed row.
- k213: `ffxiii2_diag_no_depth_test_ps` (the smoke FCF76CCFFD55FFDE without its depth test, key verified: smoke over
  Bahamut): 0.06 % and 0.28 %, and the band is still there (frame 666 of nod2: the buildings cut at y 384). **Not the
  smoke's depth test; not the smoke at all**: the whole image differs in a strip.
- k214 `[tiles]` / `[strips]` (`ffxiii2_diag_tiles_log`): only one tile layout ever ([0,0-640,720][640,0-1280,720]); a
  HUD shader (3863C05F104BF198, an animated centre banner) draws with scissor rows ~333-387 in clusters, but the
  drawlog300 window had banded frames without any such draw: not proven related. Banded frames of k214 (stamped,
  3.98 %) show **rectangular blocks of other content** (frame 1565: x 0-420 / y 336-384; frame 1564: right side above
  y 336) - the look of a texture sampled while a copy into it is still running.
- **Root cause candidate (code):** plume_deko3d `barriers()` gives the transition into a copy destination a Full
  barrier (it waits for the copy / 2D engines) but the transition back to SHADER_READ after the copy only a
  Fragments barrier, which orders the 3D engine only. The 2x MSAA scene-colour resolve is `resolveTextureRegion` =
  `dkCmdBufBlitImage` (2D engine); the effects that sample that copy (smoke blending, water refraction: the user's
  water band) can start while the blit still writes it. **Refuted by k216 `ffxiii2_deko3d_full_barriers`** (every
  barrier Full with L2 + image invalidation): 53 of 3,108 frames banded (1.71 %). The prepared
  `ffxiii2_deko3d_copy_wait` stays a diagnostic key, off (k217).
- k216 stamped strip log: 2 of the 5 banded frames (3252, 3304) fall exactly on the HUD banner's opening / closing
  frames (scissor 320-400); the other 3 came after the log's 20,000-line cap. k216 full-barrier frame 1806: rows 0-335
  show the image before a camera cut, rows 336+ the new camera: **in banded frames the top 336 rows of the final
  image are not updated** (336 = EDRAM tile rows: 21 at 1x, 42 at 2x MSAA). k205 stamped [rtlog] (121 banded vs 1,425
  clean frames): no token only in banded frames; strongest difference the 320x180 bloom ping-pong pairing
  (R3019C000>6D0573BC: 62 % of banded vs 38 % of clean frames). Open.
- **k217 bisect (14:00): the band is already in the scene-colour resolve 6D05869C** (banjo_renut_diag_show_texture,
  no post-processing or HUD on screen; stamped recording, blind start): frame 364 the cut at y 336 with the smoke
  silhouettes, frame 909 the 336-384 strip. So the fault is in the scene pass or its resolve, not later. Leading
  hypothesis: the same race as D-007 for the rebinds the fix leaves alone - the scene's headers 6CB07EB0 / 6CB07E60 are
  rewritten several times a frame (640x720 2x MSAA tile layout for the scene, 1280x720 1x for the post-processing,
  1024x1024 for the next frame's cascades) and the render thread reads them when it gets to the draws: how far it lags
  decides which host target some scene draws land in. Checked on k205 [rtlog] (121 banded / 1,379 clean frames, the
  pass from the scene clear to the scene-colour resolve): the scene draws use 6CB07EB0 / 0A010280 (the tile layout) in
  every frame: **not supported.**
- Next lead: every frame the scene resolves its depth (R...>6D059D6C / 6D059E9C [0,0-640,1440]), then draws once into
  the depth's EDRAM as a colour surface (D6CB07E60/0A000280 at EDRAM base 0x2D0, 640x720 1x), then goes on with draws
  depth-tested against 6CB07E60. On the console that colour draw writes the depth memory itself (a depth restore /
  conversion); the host keeps a separate colour target there, so the host depth never gets it.
- 2026-11-06 14:35 user: "The PC Build also shows the flicker bug. Fix the pc build before you continue with the
  switch version" (100 % as the Xbox 360). PC test so far (k196 PC, Renut on Vulkan, scripted battle, bursts of 30
  captures every 5 s, band_renut5): 0 of 510 battle frames banded (the Switch also had runs as low as 0.07 %). The
  emulated Xbox 360 GPU on the same PC build (`banjo_renderer=emulated`, band_emulated5) as the reference.
  `Works\ffxiii-2-port-main` (Triumtemporate Edition) has no renderer fixes: it runs the SDK's emulated GPU
  (rexgpu-xenos.dll from the Lightning Returns builder, ReXGlue 0.10.0, D3D12), no Renut.
- Side finding: the game samples both shadow cascades as one 2048x1024 texture (6D0590FC over the resolve
  destinations 6D0592FC / 6D0594FC); the renderer builds that one from guest memory (`[flash]`, once a run at the
  battle start): SUSPECTED shadow atlas bug, to look at with D-007.

## D-005 Frame Tap excluded FF XIII-2 (2026-11-06 ~08:50)
- Class: BUG_CONFIRMED in our test procedure (not in the game); fixed in the procedure.
- Evidence: Frame Tap log.txt [5035 s]: "055b4bdc705f6000 closed itself 1404 ms after the stream attached (anti-debug,
  as TiCo) - skipped from now on"; excluded.txt then held our title id; the k204 GC recordings got 0 packets
  ("live: ... is excluded - not attaching"). Cause: our `switch.py kill` closed the game right after a recording.
- Fix: excluded.txt renamed to excluded.txt.20261106_0859.bak (read at boot: the user reboots, or "Stream this app" in
  the overlay). Test scripts: wait 20 s after the viewer ends before closing the game.

## D-006 std::terminate (std::system_error) at start, on a fast restart (2026-11-06 09:30, renut_094)
- **2026-11-07 USER: "The Game closing during loading is a Bug with frame Tap. You can ignore it for our build."**
  Class: NOT_OUR_BUG (Frame Tap). Closed; no further work. Procedure stays: no Frame Tap stream during loads.
- 2026-11-07 (k239j, k239m, rc8): two more ends with a Frame Tap stream attached, both "the software was closed
  because an error occurred", no Atmosphere crash report and no rex_crash.log. k239j: stream attached at the title
  menu (no load running), the game ended ~80 s later right after the opening's load began (renut_138 stops mid-run,
  recording 0-59 s game, then black, then the error screen at 67 s). k239m: stream attached at the start of the
  real-time opening (second load), the game ended within ~1.5 s (Frame Tap: "This app closed when streamed - it is
  skipped from now on": the title is on excluded.txt again; undo as in D-005). Without a stream the same sequence ran
  through 8 times (k239d-i, n-q). Pattern so far: a stream attached + a load = the process ends. Visual sessions now
  use sys-botbase screenshots or the renderer's own read-back (banjo_video_s) instead of Frame Tap.
- 2026-11-06 11:25 (renut_105): the game ended during the load ~5 s after a Frame Tap stream attached (Frame Tap
  log 1484 s: "closed itself 5322 ms after the stream attached" -> excluded again); no new rex_crash.log (the one
  on the SD is the 09:30 file, byte-identical). The 09:30 terminate also came 115 ms after a stream attached.
  Frame Tap reads frames through a debug session on the game. SUSPECTED: a stream attaching during the start/load
  makes a system call fail (std::system_error) or ends the process. Procedure now: the recording starts only after
  nav reports the battle (never during loading).
- Class: STABILITY_CONFIRMED (seen once today; the same signature as the Nov 3 crash reports), cause SUSPECTED.
- Evidence: the game showed "the software was closed because an error occurred" 30 s after a start that our A-press
  loop caused from HOME (the previous instance had just been closed). rex_crash.log: `std::terminate:
  std::system_error: error`, thread 707. Symbolisation against the build's ELF (switch_builds/experimental/
  20261106_0925_e2eeb8896819/renut) gives implausible names (format.cpp static init, nv50_ir): the crash log's image
  offsets and the ELF's addresses do not line up for this thread - symbolize.py needs checking before trusting it.
- Lead: std::system_error is what std::thread / std::mutex throw on a failed creation (resource limits right after
  the previous process). Next: reproduce with two quick restarts, read the thread count.
- Fix in the tooling meanwhile: `switch.py click/seq --if-title 055B4BDC705F6000` presses only while the game runs.

## D-004 First start after a reboot / deploy did not come up (k199 06:46, k200 07:09)
- Class: SUSPECTED. k199: the forwarder (055B4BDC705F6000) opened the display, then no renut log; a HOME press 10 s
  after the launch. k200: HOME pressed 1.5 s after the launch, the app left 1.6 s later, then a reboot by the user. No
  crash report. The retries started normally (renut_077, renut_078). Asked the user whether those HOME presses were
  theirs; may also be D-001.

## D-008 PC flicker: single frames with the scene black / sky only / half black (user video 2026-11-06 16-38-50.mp4)
- Class: RESOLVED_VERIFIED in the battle (k229pc: depth copies at bind time + tiling check by origin; the k225 interim hack is off). Field (save 15) to be confirmed by the user.
- Cause: FF binds header 6CB07E60 as the scene depth (info 0A010280) and rewrites it as a 1280x720 colour view (14000500) before the render thread reads it; the renderer drew ~70 % of frames with no depth and the rest with a stale depth target (the flicker frames). Logs: out/pc_runs/flicker_bind1 ([bindlog]), flicker_skip1 ([rtlog]).
- k222 depth copies at bind time (console-like): no flicker, but a seam at x 640 (right tile differs) -> `ffxiii2_renut_stable_depth_surfaces` default off.
- Verified: fix_asdrawn1, 2,400 battle frames after Skip, 0 dark drop-outs, no seam.
- Open (separate): the opening cutscene alternates every other frame between two shots (user video 0:08-0:21; fix_intro6-16).

## D-009 Opening cutscene: Lightning's face drawn every other frame (user video 0:08-0:21)
- Class: RESOLVED_VERIFIED on the PC (k238pc; the user 2026-11-07: "It is fixed."). The user 2026-11-07: rc8 on the
  Switch still flickers in the intro. The Xbox 360 is steady.
- Switch (k239, rc8 = k238 on the console, NRO MD5 checked on the SD): the CPU side is fixed there too. The pass-1
  counts on the console (tools/switch_tests/intro_ffxiii2.py: NEW GAME, movie skipped, the PC script's timing;
  renut_passcounts.toml) hold the face (6B3BB7F0, caller 829858EC) on every swap of its shots: k239i 7 + 215, 49, 146
  swaps with the face, gaps only at camera cuts, no alternation (the bug was ".F.F" for ~130 swaps). The ARM build
  of rex_vmaxfp is the NaN-aware form (k239b: sub_8298BB78 disassembled: fcmeq/bsl/fadd). So what the user still
  sees on the Switch is another fault: sys-botbase screenshots back to back over the real-time opening (k239n
  compressed, k239o uncompressed swapchain) are black in about half the shots (black runs of 1-3 s with single
  scene frames between them); the same run's battle is not. Screenshots and Frame Tap read memory, so this needs the
  renderer's own read-back to tell a real black frame from a capture artefact (k239r: banjo_video_s, the swap
  counter g_banjo_game_frames now counted in FF's Swap hook). Not decided from pictures alone.
- k239r/s/t (the renderer's own read-back, banjo_video_s, 300 frames; k239t with [capframe] = renderer frame and
  [rtlog]): the presented frames really alternate. In consecutive renderer frames the picture switches between the
  current shot and the shot just before the cut (4191 the "Lightning / Odin!" card, 4192 the ice shot, 4193-4194 the
  card again, 4195 Odin's legs; k239r the same with the dark "globe" shot). The stale frames carry that shot's own
  captions, which the game draws in the final pass, and [rtlog] shows a normal frame for them (every Swap presents a
  front buffer resolved in that frame, age 0, the same targets and resolves as its neighbours). So the GAME draws the
  previous shot again: its cutscene time goes back and forth across the cut. New lead (D-010): the cutscene clock
  is not monotonic on the Switch, where the opening runs at 10-25 fps (PC: full speed, steady); candidates: a clock
  taken from the audio stream's position (XMA / XAudio) or from a timer that the slow frames make jump. Next: log the
  cutscene time per swap on both platforms; find the clock source.
  **SUPERSEDED 2026-11-07 k240 (see D-010):** the "stale shot" frames are the 3D scene black with only the subtitle
  layer left (out/k239t/sel.jpg), not an earlier shot; the game's draws are complete and monotonic (k239u draw chains);
  the post chain's composite draws are lost in the renderer (draw exit #4). The cutscene-clock idea is disproved.
- Cause: the recompiled CPU code. vmaxfp / vminfp (and vmaxfp128 / vminfp128) were emitted as MAXPS / MINPS (simde),
  which return the second operand when either is a NaN (and for +0 / -0); the 360's VMX returns a NaN. FF's visibility
  octree (sub_82980028 / sub_8298D928 path; node bounds rebuilt by sub_8298BB78 as a running min / max over the node's
  objects' boxes) holds an object with a NaN box (6B3C2040) in the face's node 6CF86240. The objects' order in the node
  changes every frame (6B3C2040 moves between cells 4 and 5, 6B375C70 between 0 and 1): when the NaN came before the
  last box, the running min / max restarted and the node box was only the last object's local box at the origin, so
  the node failed the frustum test and the face objects (6B3A0E50, 6B3BD5A0) were never visited (k237 vis_all /
  vis_numbers: node results N / n alternating, its box (-47, 52.8) +-(54, 52.8) vs (0, 8.67) +-(7.06, 8.67)).
- Fix: rex/ppc/intrinsics.h rex_vmaxfp / rex_vminfp (the VMX results: NaN in -> NaN out, vA's first; max(+0,-0) = +0,
  min(+0,-0) = -0); the codegen builder (sdk/src/codegen/builders/vector.cpp) emits them; the existing generated code
  rewritten with tools/minmax_emit_update.py (1,026 statements, each after its vmaxfp/vminfp comment, + 180 in the hot
  copies; the same text a new codegen gives). Verified: k238_intro 600 frames, 0 flicker frames (was 72), the face in
  the pass-1 list on every swap from the first (also the opening's "F..F..F" flashes gone); k238_battle 1,500 frames,
  0 drop-outs. Backups: sdk intrinsics.h.before_k236 / vector.cpp.before_k236 (the files before k236 = before k238).
- Dead end (k236, reverted): vmaddfp / vnmsubfp fused (tools/fma_emit_update.py, fma_forms.py, fma_hot.py,
  fma_all_plain.py). One test (intro_fma2) looked steady but did not reproduce (all-fused, VMX-only fused, 13-function
  fused, hot copies off: all alternated); the generated code is back to multiply-then-add (9,841 sites checked).
- Earlier status (k234pc): NEEDS_CONSOLE_REFERENCE; the game itself leaves the face out.
- Evidence: draw_chains (intro_n): all of the face's draws come from FF's render lists; per Swap the shadow list has
  36/20 draws and the scene pass 34/26, alternating. pass_counts (FF's pass-1 list, sub_82972D48 from 829858EC, the
  face's first part 6B3BB7F0): the same pattern on Renut (intro_p2), on the emulated GPU (intro_emu3, a much slower
  run) and with the CPU enhancements off (intro_q): 5 x every third swap, ~60 swaps steady, ~130 swaps every other
  swap, ~28 without, then steady. Frame-exact game logic, independent of speed and renderer.
- Ruled out: occlusion queries (Issue not called), fence polls, Renut draw exits (no draw lost in DrawStateLocked),
  dead-draw skips / snapshot deltas / chain reuse (intro_l), no texture from the previous frame is sampled (trace).
- Next: compare with the Xbox 360 (does the console show the face flicker there?). If the console is steady, the
  difference is in CPU-side logic shared by both renderers (the recompiled code), not in Renut.

## D-010 Switch opening cutscene: isolated frames with the 3D scene black, only the subtitle layer (rc8)
- Class: CONFIRMED (root cause, k240 analysis); fix k240 built, console verification pending at the time of writing.
- Symptom: the user, rc8 on the Switch: the intro still flickers (PC k238 steady). The renderer's own read-back (k239t)
  shows frames with the scene black and only the subtitle ("Lightning / Odin!") left; the neighbouring frames are
  normal. Earlier reading ("the game redraws the previous shot", D-009 notes) is wrong.
- Evidence (tools/visual_debug/chain_timeline.py, orphan_resolves.py, missing_draw_chains.py, log_context.py):
  - k239u (normal speed, no capture): the game's per-swap draw set is monotonic (56 sets, 1 trivial return in 300
    swaps, out/k239u/timeline.txt) - no shot goes back.
  - k239u [rtlog], 2,599 frames: the post chain's full-screen draws on colour header 6CB07E60 (info 14000500, EDRAM
    0x2D0, 7-10 a frame) are missing in ~20 frames (n2D0 1-3; e.g. 4261/62, 4279/80, 4287/88, 4296/97, 4441), while
    their resolves (3014B000 > 6D05869C / 6D0583EC / 6D05707C, 30103000 > front buffer) still run (out/k239u/orphans.txt).
  - The game-side draw chains (counted in the D3D draw hook) are complete in exactly those swaps (only +-2-4 noise in a
    particle chain; out/k239u/missing_chains.txt): the game issued the draws; they were lost in the renderer.
  - "[ffxiii2] draw exits: 0 0 0 0 469 ..." in that window: DrawStateLocked's exit #4 = GatherTargets failed (no colour
    and no depth target) for a draw that is not export-only.
  - k239t (capture video on, ~10 fps): the post chain is incomplete in most frames (the game appears to skip passes
    when slow) - a capture-speed effect; not the cause by itself.
- Root cause: FF's header 6CB07E60 is dual-role: the scene depth view (0A010280, a depth format) and, rewritten in
  place, the post chain's 1280x720 colour view (14000500). Depth bindings are bind-time copies since k229 (D-008);
  colour bindings stayed live (same height 720, so stable_rewritten_surfaces does not copy). When the render thread
  reached a post-chain draw after the game had flipped the header back to the next frame's depth view, GetHostSurface
  returned a depth surface for the colour slot -> GatherTargets false -> exit #4 -> draw dropped. The Switch's render
  thread lags the game more than the PC's: the PC showed it rarely. Same class as D-007/D-008 (audit risk R1: headers
  read late on the render thread instead of latched at bind time).
- Fix (k240): `ffxiii2_renut_stable_dual_role_surfaces` (default true, d3d/hooks/state.cpp Ffxiii2StableSurface): a
  header the game has bound with a depth format is bound as a bind-time copy in a colour slot too (console semantics:
  D3D latches the registers at bind time). Colour headers never bound as depth (the tiled scene's 6CB07EB0, whose
  copies split the scene in k207-k209) stay live. Correct frames are unchanged (the copy has the header's contents at
  bind time, the same TargetKey and host target). Cost: one 48-byte copy per such binding. Backup: state.cpp.before_k240.
- Ruled out: game cutscene clock (k239u monotonic draw sets); front buffer alternation (stale/black frames spread over
  both front buffers, out/k239t/stale_vs_front.txt).
- Verification plan: k240a = the k239u procedure with the fix (draw exits #4 -> 0, no frame with n2D0 <= 3), then a
  read-back run (renut_intro_video_rt.toml) for black frames, PC regression (battle, intro), rc9.
- PC regression k240 (2026-11-07 09:20-09:28, same runs as k238): fix active ("colour binding of 6CB07E60 ... bound
  as a copy" logged), draw exits 0 in every 10 s window of intro and battle, self-checks 0 differences. Band dumps
  scored with tools/visual_debug/flicker_pgm.py (pure Python, PC has no numpy): intro 1/600, battle 4/1500 over the
  0.15 threshold, 0 dark drop-outs; the flagged frames checked by eye (out/k240_flag_intro.png, k240_flag_battle.png):
  camera cuts and hit / white flashes, picture complete - no regression on the PC.
- 2026-11-07 09:30: k240 deployed to the Switch as the current build at the user's request; the user tests the intro.
  Console verification by the k240a procedure still to do (not run while the user plays).
- User (~09:45): "better but not fully fixed". **k241 (the class, CONFIRMED on the PC):** the new validator
  `ffxiii2_diag_late_headers` found another live header read late with the k240 rules: the post chain's **6CB07E10**,
  bound in a colour slot as 320x180 (size 04FC0598, pitch 0x140, base 0x5A0 - its first contents, so the height rule
  kept it live) and read by the render thread as the 640x360 view (09FC0B38, base 0x5A0) the game had rewritten it to:
  270 reads / 10 s on the PC at normal pace, 1,647 with `ffxiii2_diag_render_lag_us=1500` (the PC lagging like the
  Switch). Those draws land in the other view's host target. l1 (lag, dual_role off) reproduced exit #4 on the PC too
  (the D-010 mechanism), so the lag key is a valid stand-in for the Switch's timing. **Fix: `ffxiii2_renut_stable_all_surfaces`
  (default on since k241c): every heap header binding is a bind-time copy (per header and contents).** k241 matrix:
  l3 (lag + key) 0 late reads, exits 0; b3 (battle + key) 0 late reads, 0 flicker frames of 1,500, 0 dark drop-outs,
  seam step at x 640 1.215 (k240 1.229: no seam), 58 copies. Switch: pending (k241c build, then the user).
- **2026-11-07 ~10:50 USER on the Switch (k241, current build): "The switch build works no flicker." D-010 RESOLVED_VERIFIED
  (PC and Switch).**

## SA-1 Shadow atlas built once from guest memory (the D-003 side finding)
- Class: BUG_CONFIRMED; fix verified on the PC (k243d), Switch build made, not deployed.
- Cause: the game samples both shadow cascades as one 1024x2048 texture (6D0590FC, and the 6D05921C view in format 6)
  laid over the two cascade depth resolve destinations (6D0592FC + the next 1024 rows). Those are GPU-only resolves;
  the renderer built the big texture from guest memory once (`[flash]`, at the battle start), so the shadows sampled
  a stale atlas for the rest of the battle.
- Fix (textures.cpp, key `ffxiii2_renut_compose_resolves`, default true): a 2D texture whose rows are covered exactly
  by depth-resolve destinations of the same width is composed on the GPU from those resolves (copyTextureRegion per
  part), recopied only when a part's resolve count changes. Same contents the console reads from that memory.
- k243 (first version) stalled the render thread (signature with the resolve frame + a texture-epoch bump per copy +
  depth-word conversion inside the copy loop: 63 ms/frame, then black). k243d removed those.
- k243d PC battle (300 s, 1,500 dumped frames): render thread 0.3-5.3 ms a frame (k243 off: ~1.5-5 ms), no stall,
  5,000 copies by ~2 min (about one per atlas and frame), contact sheet frames 9000-10200 complete; flicker frames
  2 (k243 off: 5), 0 dark drop-outs; seam ratio 1.20 (k241: 1.215). Open: the user's look at the shadows.
