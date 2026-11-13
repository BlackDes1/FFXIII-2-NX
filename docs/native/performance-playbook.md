# Performance playbook — everything done to make FFXIII-2 run at ~30 fps on the Switch

Written for other sessions and other recompiled Xbox 360 -> Switch ports (ReXGlue / rexglue SDK + the Renut renderer on
deko3d). It lists, in one place: how we measured, what was changed, what each change gave **on the real console**, what
was tried and rejected, and what is still open. Newer detail lives in the dated notes linked at the end.

Scope of the numbers: Final Fantasy XIII-2 (Xbox 360, EU, TU6), handheld, 1280x720 panel. "S1b" = the New Bodhum field
from Save Data 01, 120 s runs, 10 s log windows. Stock clocks are CPU 1020 MHz / GPU 460 MHz unless a row says otherwise.
Nothing here is an emulator number: Ryujinx was only used to prove a build boots.

---

## 1. Where the time goes (know this before changing anything)

- The game's frame is **serial on its main thread**: shadow job, render job (~15 ms of draw submission), follow-ups, then
  the update for the next frame. In S1b the main thread runs ~44 ms of jobs a frame at 1020 MHz, 34 ms of them while the
  three job workers are idle. So the first wall is **guest code on one core**, not the GPU.
- The second wall is the GPU: 28-39 ms a frame at 720p in heavy scenes, 22-30 ms at 612 lines.
- Beside them: the XAudio mixer thread (~45 % of a core; its biquad was 25-40 % of that thread) and the XMA decoder.
- A frame that is only slightly late became a 50 ms frame, because the game waits for a vertical blank on a fixed 60 Hz
  grid (section 4.1). That, not raw speed, was the "sudden drop to 20 fps".

Consequence: gains came from (a) cheaper generated code and where it sits in memory, (b) taking work and waits off the
main thread, (c) a lower internal resolution for the GPU, (d) frame pacing, (e) clocks as a user option.

---

## 2. How to measure (the rules that kept us from fooling ourselves)

1. **Only compare inside one session, alternating A B B A.** Run-to-run noise on the console is ~0.3-0.5 fps; use >= 4
   pairs for an effect below ~3 %. A single pair proves nothing.
2. **Normalise by draws per frame** when the view is not fixed (S1b varies 450-484 draws a frame; ~0.14 ms per draw).
3. **Check the clock the run really had.** One whole profile was taken at 1020 MHz by mistake because a settings restore
   dropped the clock key. The log prints the clocks; read it.
4. **Know what the save loads.** For three sessions "the newest save" was an opening cutscene, not a field; every number
   taken there described the cutscene. Look at a screenshot of the run before trusting its numbers.
5. **A view that already holds 30 fps cannot show an improvement.** Test in a heavy view (many draws, moving), and compare
   the main thread's busy share, not only fps.
6. **Pictures are evidence beside the log, never the verdict alone.** Each renderer bug here was settled by a log counter
   (sections 5.3, 5.4), the pictures only showed that something was wrong.
7. Never two builds at once, never a build during a game run, back up and compare the saves around every console run.

### Tools in this repo / used on the PC
- 10 s log line of the renderer: game frames, fps, render-thread ms, GPU ms, draws. Needs `log_level = "info"`,
  `log_file_writer = true`, `log_file_reopen = true` **at the top** of `ffxiii2.toml` (keys written after a `[table]`
  header belong to that table and are ignored).
- Stack sampling on the console: an empty `logs/rex/perfil_pilas.flag` on the SD card turns it on; symbolize with the ELF
  of the **same build** (`tools/switch_hot_symbols.py`, `tools/switch_hot_stacks.py`).
- Per-frame surface log: `ffxiii2_diag_rt_start_s = N` + `ffxiii2_diag_rt_frames = M` prints, for M frames after N
  seconds, every surface drawn, its draw count and every resolve. This found the blurry-text cause in one run.
- Guest job census / timeline: `ffns_job_census`, `ffns_job_timeline_at` (which guest jobs run on which thread, and when).
- Console automation over sys-botbase: start the game, load a save tile, hold the stick, take screenshots, fetch logs.

---

## 3. CPU: what was changed and what it gave

| # | Change | Result on the console | Kept? |
|---|---|---|---|
| P1 | **ABI-lifted code generation**: guest arguments in host registers (`args_in_registers`), non-argument registers as locals, register diet | 10.56 -> 15.8-16.1 fps in S1b (**+52 %**) | yes — the largest single gain |
| — | 1,296 bulk "machine copies" of hot functions (`app/src/cpu/hot/localized_bulk.txt`) | +4 % | yes |
| P2 | Post-passes: read-only constants, single floats, inlined leaves | no measurable effect | constants kept, inlining reverted |
| P3 | **XMA decoder thread on core 3** | -3.5 +- 1.0 ms a frame (~+6 %) | yes |
| P3 | Mixer on core 3; raised main-thread priority | worse / no effect; core 3 near 100 % made the console drop off the network | no |
| P4 | Job / effect worker priorities | n.s.; raised job workers hang | no |
| P5 | Promoted indirect calls (direct calls where the target is known) | -0.53 +- 0.16 ms (~1 %) | yes |
| — | **Window blocks**: the guest address window on 2 MB-aligned blocks | -3.44 +- 0.60 ms a frame (~6 %) | yes |
| — | **Code blocks**: NRO text / rodata on 2 MB blocks | +0.81 fps (~4.3 %), costs ~62 MB of heap | yes |
| — | clang instead of GCC for the generated code, exact float mode | -8 % | no (GCC 16.1 stays) |
| — | One fixed FPCR mode (not exact) | 18,352 -> 35 mode switches, **no fps gain** | off |
| p11 | **PGO + LTO** of the whole Switch build (`pgo/` holds the profile) | 19.7 -> 25.5 fps at 1020 MHz; ~28.9 fps at 1428 MHz | yes |
| — | Native XAudio biquad `sub_82A34BB0` (`app/src/ffxiii2/ffxiii2_audio_native.cpp`) | bit-exact (11.7 M samples checked on the host), 1.35-2.1x on x86; mixer-thread share on the console not measured separately | yes |
| — | Main thread alone on a core (`sa2_renut_core_layout`) | 27.9 / 28.4 vs 28.9 fps | no |
| — | Ring thread on core 3 | no effect (core 3 is already 60 % busy) | no |
| — | `switch_yield_backoff` (polling yields sleep 100 us; from renut_nx_ultimate_base port 56) | no verdict: the test view held 30 fps either way | key exists, off |

Lessons:
- **Change how code is emitted, not the architecture.** Static AOT stayed; a JIT or a rewrite was never needed.
- **Memory layout is a real lever on the Switch** (the two "blocks" rows: ~10 % together) — TLB reach, not instructions.
- **A profile must exist before PGO**, and it goes stale: re-train after large code changes; never mix streams.
- A hand-written native for a hot leaf is only worth it when it can be proven bit-exact off the console first. Pattern:
  compute the fast form on the dependency chain, the generated code's form beside it, and redo the rare sample that differs.
- Thread placement: move a *separate* consumer (the decoder) to the spare core; do not move things the main thread waits
  on, and never fill core 3.

---

## 4. Frame pacing and waits

### 4.1 Late flip (`ffns_late_flip`, default on, exact picture)
The game waits each frame for its Swap's flip, set from the emulated vertical blank (target = last flip's blank + 2). With
blanks on a fixed 60 Hz grid a frame of 33.4 ms waited ~8.3 ms more for the next blank: 50 ms = 20 fps, and 17 % of the
main thread was that wait in the bad seconds. Now a Swap that arrives two or more blank periods after the game's last
flip gets a blank at once. Guards that were needed:
- decide lateness on the blank thread, not at the Swap (the Swap can arrive before the flip is recorded: the first build
  ran menus at 60 fps);
- only while the game flipped in the last 250 ms (loading and menu screens pace themselves another way).
Result: the frame-event wait of a late frame 9 ms -> 0.65 ms; the 20 fps steps are gone. On averages in a view that is
late every frame the gain is small; its effect is on frames just over 33 ms.

### 4.2 What the main thread still waits for (heavy view, CPU 1581 MHz, main thread 91 % busy)
effect-library sync `sub_82BE0698` 4.0 %, job wait loop `sub_82886578` 2.9 % (66,000 yields a second), `sub_82252A28`
1.9 %, two more below 1 %, frame event 0.2 %. These are the open CPU candidates (section 8).

---

## 5. GPU and picture: what was changed and what it gave

| Change | Result | Notes |
|---|---|---|
| **Fixed 612-line internal resolution** (`ffns_dynres_fixed_height = 612`, default) | GPU 34.1 -> 27.9 ms in S1b | fps unchanged there (CPU-bound), but it removes the GPU wall in heavy scenes |
| Dynamic resolution with level changes | corrupted the picture once (rainbow characters, blocky backgrounds); cause not found | **not shipped**: only the fixed height |
| Far shadow map every 2nd frame (`ffns_shadow_far_every = 2`) | -1.3 ms a frame, GPU -0.8 ms, 55 draws fewer | menu option; see 5.4 before relying on it |
| Upscaling: bilinear / + sharpen / FSR 1 | GPU 27.7 / 29.2 / 34.1 ms | FSR costs +6 ms: an option, not a default |
| GPU clock 537 MHz | GPU ms -3 in the light phase, **no fps gain** in the heavy one | proved the heavy view is CPU-bound |
| Draw distance cuts | nothing to cut in S1b | option only |
| Render side overlapped with the next update (P7) | unsafe: shared device and mesh nodes | rejected |

### 5.1 Text and HUD at the display's resolution (`ffns_ui_native`, default on)
With 612 lines everything, HUD included, was drawn small and scaled up: text was soft. Fix: after the game's composite
pass the 3D image is scaled once into a 1280x720 target and the HUD, the text and the front-buffer resolve use that
(`render/ui_scale.cpp`, `render/surfaces.cpp`). Cost: within run noise (GPU +0-1 ms). So **612 lines for the 3D image
costs no text quality** — this is what makes the lower resolution acceptable.

### 5.2 The 2D screens needed a second step
Frames without a composite (boot box, title, save / load) draw their content into one surface, resolve it to a texture
and draw that texture into the surface that goes to the front buffer. Only the last surface had the 720p target, so the
text was drawn at 612 lines and scaled twice (thin font strokes broke up). Now the first other whole-screen colour
surface drawn or cleared with no depth buffer bound in such a frame is at 1280x720 too. Found with the per-frame surface
log (section 2), checked against a run at true 720p.

### 5.3 A "saving" that broke the picture: depth hand-over
`banjo_deko3d_depth_handover` (inherited from another port) skips a depth resolve and lets the texture take the target's
image at the next clear. In this game the two shadow maps were handed over a few seconds after the player moved and were
then sampled stale: every shadow vanished or stood in the wrong place on the field (battles were fine). The 10 s log line
"depth hand-over ... N resolves not drawn" read ~2 a frame exactly when it broke. **Off by default now.** Lesson: an
inherited GPU shortcut must be checked in this game's own passes, moving, not standing still.

### 5.4 Lesson from the shadow hunt
Three wrong fixes came first (far-map skip rule, late flip, UI target), because the bug could not be reproduced: the test
save was a cutscene and the test stood still. It was found within one run once a field save was loaded and the stick was
held. Reproduce first; a fix for a bug you have not seen is a guess.

---

## 6. Clocks (user option, off by default)

`ffns_cpu_mhz`: 1020 (stock) / 1224 / 1428 / 1581 / 1785 MHz; `ffns_gpu_mhz`: 460 (stock) / 537 MHz. Set by the game itself
through clkrst, held by a small thread on core 3, put back at exit; no sys-clk needed.
- **Open one clkrst session per clock and share it.** Up to six sessions, beside other clock tools on the console, ran the
  `pcv` service out of session memory (Atmosphere fatal 2011-0102). Three shared sessions fixed it.
- Stop re-applying when another tool keeps resetting the clock (give up after two 10 s windows with > 25 resets).
- What the clock buys: p11 at 1020 -> 1428 MHz = 25.5 -> ~28.9 fps in S1b; the opening battle holds 30.00 at 1428.
  1581 did not close the heavy views; 1785 was added as an option and not measured.

---

## 7. Where we are

| View | Clocks | Result |
|---|---|---|
| S1b at the start of the native project (old code generation) | 1020 / 460 | 10.56 fps |
| S1b, p11 (all CPU work + PGO/LTO) | 1020 / 460 | 25.5 fps |
| S1b, p11 | 1428 / 460 | ~28.9 fps (many windows 29-30) |
| Opening battle | 1428 / 460 | 30.00 fps |
| New Bodhum heavy view, standing (410-450 draws) | 1428 / 460 | 28.9 fps mean of 6 runs, main thread 93 % busy |
| Opening cutscene, heavy phase (400-450 draws) | 1581 / 537 | 25.8-28.5 fps, main thread 91 % busy |
| New Bodhum beach at night, running | 1581 / 537 | 26-29.5 fps, GPU 27-30 ms |
| Same place, standing | 1581 / 537 | 29.75-30.02 fps, GPU 22-23 ms |

**Stable 30 fps is not reached in the heavy views.** They are bound by the game's main thread (about 10 % too much work
at 1581 MHz); light views, battles and menus hold 30.

---

## 8. Open candidates (not done)

CPU, in the order they match the profile:
1. The job wait loop `sub_82886578`: block on the workers' signal instead of spinning with yields (`switch_yield_backoff`
   is the cheap version; measure it in a heavy view by the main thread's busy share).
2. Effect-library sync `sub_82BE0698` (4 %).
3. `ObjectTable::LookupObject`: 103,000 lookups a second through one mutex, 7 % contended.
4. From `renut_nx_ultimate_base` `docs/ultimate-fixes.md` (not in this tree; our base is older): event waits on an
   address (port 65), XMA kick without the context lock (120), SDK objects in the LTO/PGO build (42 / 89), lock-free
   render queue (105), render thread cleaning upload ranges (80), single-rounding fmadds (96, not exact).
5. Re-train PGO on the current code. The reverb `sub_82BB4008` (26 % of the mixer thread) as a native.

GPU: the cost of hand-over off was not measured; a hand-over that provably never touches a shadow map could win it back.
The dynamic-resolution level-change corruption is not root-caused.

## 9. Do not repeat
Render side concurrent with the update; clang for the generated code; one fixed FPCR mode; mixer on core 3; raised
main-thread or job-worker priorities; main thread alone on a core; ring thread on core 3; inlined leaves on the fast
bodies; fused vmaddfp; draw-distance cuts in S1b; conclusions from Ryujinx; conclusions from a view that already holds 30.

## 10. Dated notes
[performance.md](performance.md) (targets, protocol) · [profile_s1b_2026-11-09.md](profile_s1b_2026-11-09.md) ·
[30fps-steps123-2026-11-10.md](30fps-steps123-2026-11-10.md) · [refs-20261110-review.md](refs-20261110-review.md) ·
[switchwaker-review.md](switchwaker-review.md) · [session-2026-11-12-13.md](session-2026-11-12-13.md) ·
[rendering-playbook.md](rendering-playbook.md) · [rendering.md](rendering.md) · [architecture.md](architecture.md) · [progress-log.md](progress-log.md)
