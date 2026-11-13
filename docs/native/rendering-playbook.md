# Rendering playbook — everything it took to get the picture right

Written for other sessions and for the next port on the same engine family (Final Fantasy XIII uses the same Crystal
Tools generation as XIII-2, so expect the same habits: tiled scene passes, surface headers rewritten in place, shadow
maps resolved every frame, shaders packed in the game archives). It collects, from the first PC frame to the Switch
release: how the renderer is put together, every rendering defect we met with its **cause class** and fix, the rules
that came out of them, the diagnostics that found them, and what is still open.

Stack: Xbox 360 game statically recompiled (ReXGlue / rexglue SDK) -> the game's own statically linked D3D is hooked ->
the Renut renderer (reNut's renderer) -> plume -> Vulkan (PC) or deko3d (Switch). The SDK's emulated Xenos GPU exists
as a second renderer for comparison (`--banjo_renderer=emulated`).

Sources: the notes of 2026-11-02..07 (old tree, builds k1xx-k243, release candidates rc1-rc8) and of 2026-11-08..13 (this
repository). Where a statement was not re-checked in this repository it says so.

---

## 1. How a frame gets to the screen

1. **Recompiled PPC** calls the game's D3D functions.
2. **Hooks** (`app/src/renut_engine/gpu/d3d/hooks/*.cpp`) on draws, clears, resolves, Begin/EndTiling, SetRenderTarget,
   SetDepthStencilSurface, shader setters, Swap, occlusion queries. The hook addresses were **remapped from another
   game built with the same XDK** (Zuma's Revenge, XDK 2.0.20871) by matching the D3D library's functions
   (`tools/ffxiii2_renut_hook_remap.py`). A game on the same XDK version can reuse this method directly.
3. **Device snapshot on the game thread**: each draw queues a delta of the guest device structure (13,088 bytes). The
   snapshot holds surface-header *addresses*, not contents (this matters, section 3.1).
4. **Render thread** (`backend/render_thread.cpp`, `render/draw.cpp`): finds the host target for the bound surface
   (`GetHostSurfaceLocked`), builds pipeline and descriptors, draws.
5. **Host targets** (`render/surfaces.cpp`): one host texture per EDRAM *view*, keyed by
   `EDRAM base | pitch | MSAA | depth flag | scale | host format`.
6. **Resolves** copy a host target into the host texture of the destination guest texture; tiled resolves read their
   tile from the full-size target.
7. **Back end**: plume -> Vulkan / deko3d; shader variants compiled by uam on the Switch, cached in `deko3d_shaders.bin`.

---

## 2. Bring-up order that worked

1. Boot to the first Swap on PC with the emulated GPU (proves code generation and kernel layer).
2. Hook map for Renut, PC first. PC Renut must match the Xbox 360 (xenia as a second witness, never as the judge of
   performance) before any Switch work: a defect seen on both renderers is in the shared recompiled code, not in the renderer.
3. Switch build, UI first: "do not add features while basic UI rendering is broken".
4. Shader coverage (section 5), then audio, saves, then performance.
5. Only then picture-changing options (lower internal resolution etc.), each as a setting.

---

## 3. Defect classes and their fixes

### 3.1 Class C2 — surface headers read too late (the largest class: four bugs)
The console's D3D latches the render-target registers from a surface header **when it is bound**. The game rewrites its
headers in place between draws (the same header is a 640x720 2x-MSAA tile view at one moment and a 1280x720 view the
next; a depth header is rebound as a colour view). The renderer read the header when the render thread reached the
draw, i.e. later.
| Bug | Symptom | Fix |
|---|---|---|
| D-007 | shadow cascades drawn into the scene depth | header copied at bind time when its height changed |
| D-008 | PC: single black / sky-only frames in battle | depth bindings always copied at bind time |
| D-010 | Switch intro: single frames with the 3D scene black, subtitles only | colour slot of a header also bound as depth: copied |
| split-screen seam | scene split at x 640 once copies were used | tiling check compares surfaces **by origin header**, not by address |

**Rule:** every binding is the header's contents at bind time; identity is the origin header plus contents, never a
host or copy address. One general rule replaced the three special keys. Keep the origin comparison for tiling: without it
the copies split the picture.

### 3.2 Class C1 — vector float semantics decided what was drawn
D-009: the intro's face appeared only every other frame, on both renderers. Cause: `vmaxfp` / `vminfp` were emitted as
host min/max; the PowerPC forms differ for NaN and for +0 / -0, and the game's visibility lists depended on it. Fixed in
the code generator for all sites (1,026 + 180 in hot copies): NaN in -> NaN out, max(+0,-0) = +0, min(+0,-0) = -0.
**Rule:** VMX float operations must match the console bit for bit; they are rendering correctness. A flicker that both
renderers show identically is a CPU-semantics bug. (Fused multiply-add for `vmaddfp` was tried and reverted: no effect.)

### 3.3 Tiled rendering (predicated tiling)
The scene is drawn as two 640x720 2x-MSAA tiles over one host target grown to 1280x720. BeginTiling / EndTiling are passed
through and noted; a draw predicated to a tile writes only that tile; tiled resolves read their own tile; the viewport
clamp applies only while the bound surfaces are the tiling surfaces (compared by origin).

### 3.4 Textures laid over several resolve destinations (SA-1)
A 2048x1024 shadow atlas covers the guest memory of two resolve destinations. A texture built once from guest memory
over such a range showed stale data; such textures are composed from the resolve destinations ("composed texture" in the
log). Fixed on PC in the old tree; **not re-verified on the Switch**.

### 3.5 Things that are deliberate deviations (keep the list)
- Occlusion queries are answered with a fixed sample count.
- A depth target smaller than its colour target is grown to it.
- The gamma setters are passed through.
- deko3d **push constants are off** for this game: with them on the UI was wrong on the Switch (found by bisecting the
  enhancement keys in groups). Root cause unknown.
- `banjo_noncpu_enhancements = false` breaks the picture: never use the master switch to bisect, switch keys in groups.

### 3.6 Shortcuts inherited from other ports that broke this game
- **Depth hand-over** (`banjo_deko3d_depth_handover`): skips a depth resolve and lets the texture take the target's image
  at the next clear. Here the two shadow maps were handed over a few seconds after the player started moving and were
  then sampled stale: every shadow on the field vanished or stood in the wrong place (battles unaffected). The log line
  "depth hand-over ... N resolves not drawn" read ~2 a frame exactly when it broke. Off by default now.
- **Far shadow map every Nth frame** (`ffns_shadow_far_every`): the first rule skipped "the second shadow call since the
  Swap"; the calls come from the job system, so a Swap between them made the *near* map the skipped one. Maps must be told
  apart by their object, and the option stays a user choice.
**Rule:** every inherited GPU saving is checked in this game's own passes, **while moving**, before it is a default.

### 3.7 Lower internal resolution without losing the picture
- Shipped: a **fixed** 612-line 3D image (`ffns_dynres_fixed_height`). Changing the level while playing corrupted the
  picture once (rainbow characters, blocky backgrounds); that cause was never found, so no dynamic steps are shipped.
- Text and HUD stay sharp through a **UI target** (`ffns_ui_native`, `render/ui_scale.cpp`): after the game's composite
  pass (found by its pixel-shader hash) the 3D image is scaled once into a 1280x720 target; HUD, text and the front-buffer
  resolve use that for the rest of the frame.
- 2D screens (no composite pass) draw into one surface, resolve it to a texture and draw that into the surface that goes
  to the front buffer. **Both** surfaces must be at the display's resolution, or thin font strokes break up. The second
  one is taken as the first other whole-screen colour surface drawn or cleared with no depth buffer bound.
- The scale pass must run at the **entry** of a draw or clear; run in the middle of a draw it garbled the save thumbnails.
- A depth buffer smaller than its colour target is dropped for that draw; clears of mismatched scales are split.

### 3.8 Frame presentation
The game paces itself on the emulated vertical blank; a late frame must get its blank at once, or it waits a whole
period (see the performance playbook, "late flip"). Menus and loading screens pace themselves differently: any pacing
change must be limited to frames where the game really flipped recently, or menus run at 60 fps.

---

## 4. Not rendering bugs, but they looked like it

| What was seen | What it was |
|---|---|
| Intro "flicker" left on the Switch after the face fix | first read as "the game draws the previous shot again" (a cutscene-clock theory). Wrong: the composite draws of those frames were dropped by the renderer (a header bound as colour that was also bound as depth, class C2, D-010), so an older image stayed. The draw-exit counters would have shown it; check them before blaming the game |
| Black frames in Frame Tap recordings and sys-botbase screenshots | the capture tools read a compressed swapchain. Use the renderer's own read-back (`banjo_video_s`) and map captures to renderer frames |
| Game ends when a stream attaches during a load | process lifecycle (the streaming tool), not rendering |
| Frozen picture, 0 fps after a boss | every shader variant compiled on first use blocked draws: needs the complete shader set and async variants |
| Menu item CONTINUE missing | 64-bit export arguments were truncated (the profile id): a kernel-layer bug |
| "Text is blurry, it is not the resolution" | it was the resolution, in one surface nobody had looked at (3.7) |

---

## 5. Shaders (Switch)

- Pipeline: game shader containers -> XenosRecomp -> DXC -> SPIRV-Cross -> GLSL passes -> `deko3d_glsl.bin` (in the NRO)
  -> uam -> DKSH variants in `deko3d_shaders.bin` (tag `uam110-sw8-bnx4`).
- **Dump every container from the game files up front.** This game keeps its shaders in zlib streams inside the
  `*.x360.bin` archives (streams start with `78 DA`); a scanner (`tools/ffxiii2_scan_shaders.py`) found ~26,000. Shaders
  captured only while playing covered a few percent and left compile stalls everywhere.
- Ship the complete variant cache (505 MB here): 0 draws waiting for a compile. A small cache was tried and rejected.
- Async variants on (a missing variant is compiled in the background while the generic one draws).
- A few dozen shaders fail DXC (undeclared register); allow failures in the batch instead of stopping it.
- Host shaders (UI scale passes, FSR, SMAA) are not in that cache and compile at first use.

---

## 6. Diagnostics that found the causes (all off by default, near-zero cost when off)

| Tool | What it answers |
|---|---|
| `ffxiii2_diag_rt_start_s` / `_rt_frames` (`[rtlog]`) | one line a frame: every surface drawn with its draw count, every clear and resolve, the Swap's front buffer. Found the 2D-screen layer and proved the renderer presented fresh frames in the intro |
| `banjo_video_s` / `banjo_video_frames` + `[capframe]` | the renderer's own read-back, each capture mapped to a renderer frame. The only trustworthy picture evidence |
| `ffxiii2_diag_pass_counts`, `ffxiii2_diag_draw_chains_at` | what the *game* submitted per Swap, with guest call chains: separates "game did not draw it" from "renderer lost it" |
| `[drawexit]` counters ("draw exits") | draws the renderer dropped, by reason |
| `ffxiii2_diag_bind_log_*` (`[bindlog]`), `ffxiii2_diag_tiles_log` | bindings and tiling as they happen |
| 10 s counter lines (depth hand-over, depth resolve words, UI target, composed textures) | whether an optimisation is actually acting. The shadow bug was one of these counters going from 0 to ~2 a frame |
| Differential runs: Renut vs emulated GPU, PC vs Switch, xenia as witness | where a fault lives: both renderers = shared code; PC right and Switch wrong = back end or timing |
| RenderDoc on the PC build, band trap | a single bad frame on PC |

Method rules: a picture never decides alone, read the log beside it; reproduce before fixing (three wrong shadow fixes
came from testing a cutscene while standing still); fix the class, not the symptom; never claim a test that was not run.

---

## 7. Invariants (the contract; check new code against them)

1. Bind-time latch for every surface binding.
2. Identity by guest semantics: origin header + contents; EDRAM views by base / pitch / MSAA / format.
3. EDRAM persists: rewriting or dropping a header does not destroy a view's contents.
4. One EDRAM, many views: a write through one view is visible through overlapping views. Only clears are modelled; model
   more per case, with evidence, never blind.
5. Tiled passes write only their tile; tiled resolves read their tile.
6. A texture sampled after a resolve sees the whole resolved rectangle; textures over several destinations are composed.
7. No visual change by default; picture-changing options are settings.
8. Diagnostics cost nothing when off.
9. VMX float semantics are part of rendering correctness.

---

## 8. Open or unverified

- Dynamic-resolution level changes corrupt the picture (cause unknown); only the fixed height is safe.
- EDRAM aliasing beyond clears (invariant 4) is a known gap. The old "smoke band" at y 336 on the Switch (a strip from an
  earlier frame in 0.07-4.8 % of battle frames) pointed there; last measured 0 of 2,062 frames in the old tree, never
  re-measured here.
- deko3d push constants break the UI (worked around, cause unknown).
- Shadow atlas on the Switch, teal dialog panels and the Primer picture from the first Switch builds: not re-checked.
- With the UI target on, the battle HUD, pause menu and cutscene subtitles were not looked at one by one.
- The GPU cost of depth hand-over off was not measured.

---

## 9. Checklist for the next game (Final Fantasy XIII)

1. Same XDK? Then remap the D3D hooks by library matching and expect the same device layout; verify the device offsets
   (render targets, depth, shaders, declarations) before trusting any hook.
2. Bring the PC build to match the console first, with both renderers available for differential runs.
3. Put the bind-time rule (3.1), the tiling-by-origin check and the NaN-exact VMX min/max (3.2) in **from the start**;
   they cost four separate bug hunts here.
4. Find the game's composite pixel shader early (needed for the UI target and for SMAA placement) and log one `[rtlog]`
   frame of each kind of screen: field, battle, cutscene, 2D menu, loading.
5. Scan the archives for every shader container before the first Switch run; build the full variant cache.
6. Switch defaults: push constants off until proven, depth hand-over off, no dynamic resolution steps, async variants on.
7. Test every optimisation while moving on a field, in a battle, in a cutscene and in a menu, with the log open.
8. Keep a bug ledger with statuses (unverified / investigating / confirmed / fixed / regression-tested) and a cause class
   per entry; most new bugs fall into an existing class.

## 10. Related notes
[rendering.md](rendering.md) (invariants, defect state, GPU budget) · [architecture.md](architecture.md) ·
[performance-playbook.md](performance-playbook.md) · [session-2026-11-12-13.md](session-2026-11-12-13.md) ·
[options-menu-and-dlc.md](options-menu-and-dlc.md) · [progress-log.md](progress-log.md)
