# FFXIII-2 port: architecture and risk audit (started 2026-11-07, after D-009)

Work order: the user's audit brief of 2026-11-07 (systemic causes, not symptoms; pixel-identical; console decides
performance). This file keeps confirmed facts, hypotheses, disproved hypotheses and open questions apart.

## 1. Where a frame's picture can go wrong (the shared layers)

| Layer | Shared by both renderers? | Notes |
|---|---|---|
| Recompiled game code (`app/generated`, rexglue codegen) + hot copies (`app/generated/hot`, localized natives) | yes | D-009 lived here (vmaxfp / vminfp NaN). Visibility, animation, culling, camera, all CPU-side math |
| ReXGlue runtime (kernel, memory, threads, XMA, interrupts) | yes | vblank / swap interrupts are Renut-specific glue (`ffxiii2_renut_glue.cpp`) |
| D3D hooks (`renut_engine/gpu/d3d/hooks`) | Renut only | the emulated GPU runs the game's own D3D + PM4 |
| Renut render thread, host surfaces / textures, deko3d / Vulkan backends | Renut only | D-007, D-008 lived here (headers read late) |
| Emulated Xenos GPU (SDK) | emulated only | slow; used as the diagnostic reference |

Method rule (from D-009): when both renderers show the same wrong thing, first compare the *game's own decisions*
(draw lists, visibility, per-frame counts: `ffxiii2_diag_pass_counts`) between them; identical decisions point to the
shared CPU layer.

## 2. Confirmed systemic defect classes

### C1. VMX semantics in the codegen (CONFIRMED, D-009)
- vmaxfp / vminfp (+128 forms) were MAXPS / MINPS: NaN and +-0 gave x86's "second operand". Fixed k238 (all 1,026
  sites + 180 hot copies; codegen builder; `rex::ppc::rex_vmaxfp / rex_vminfp`).
- Audit of the other VMX float instructions FFXIII-2 uses (counts from the generated comments):

| Instruction (sites) | Emulation | VMX difference | Status |
|---|---|---|---|
| vmulfp128 (10,737), vaddfp/vsubfp (~5,200) | MULPS / ADDPS / SUBPS | none known (round to nearest, flush mode set) | OK |
| vmaddfp / vmaddcfp128 / vmaddfp128 (9,841), vnmsubfp (2,818) | multiply then add | VMX may be fused (one rounding). k236 test: fused did not change D-009; reverted. No evidence either way | OPEN, low |
| vrsqrtefp128 (1,655), vrefp (352) | exact 1/sqrt, 1/x | hardware gives a ~12-bit estimate; code that uses it without Newton refinement sees other bits | OPEN: needs the 360's estimate tables (no source) |
| vmsum3fp128 (1,290), vmsum4fp128 (365) | DPPS (x86) / simde pairwise (ARM) | summation order / rounding may differ between PC and Switch and from the 360 | OPEN, low |
| vcmpeqfp / gtfp / gefp (+record) | CMPxxPS | NaN false, as VMX | OK |
| vcmpbfp | CMPGT / CMPLT | NaN in bounds (VMX: out) | not used by FFXIII-2 (0 sites) |
| vctsxs / vcfpsxws128 / vcfpuxws128 | helpers with NaN -> 0 | as VMX | OK |
| vexptefp128 (2), vlogefp128 (1) | polynomial / log2f | estimates differ | negligible use |
| vrfin/m/p/z | ROUNDPS | as VMX | OK |
| scalar fmadd family | std::fma | as PPC (fused) | OK |

### C2. Device state read late by the render thread (CONFIRMED, D-007 / D-008, fixed k210 / k229)
- FF rewrites surface headers in place after binding them; Renut read them when it drew. Fixed by binding a copy of
  the header at bind time; tiling identity compared by origin (`native::SurfaceOrigin`).
- Audit of identity by address after the copies: host surfaces are keyed by `TargetKey` (EDRAM base, pitch, format,
  MSAA, scale: `GetHostSurfaceSlow`), not by the header's address, so a copy and its original alias the same host
  target (checked in `render/surfaces.cpp`). The only address comparison of bound surfaces found so far was
  `TilingBoundSurfaces` (fixed). OPEN: `g_targets_reuse` (`render/draw.cpp`, compares header VAs + their words: a copy
  is a new VA, so only a missed reuse, not a wrong one); the resolve path (`ResolveStateLocked` keyed by source VA).
- **Third instance (CONFIRMED, D-010, fixed k240):** colour bindings were still read late. FF's 6CB07E60 is both the
  scene depth view and the post chain's 1280x720 colour view (same height, so the height rule never copied the colour
  binding). A late read found the next frame's depth view: `GatherTargets` exit #4, the post-chain composite draws
  dropped while their resolves ran (Switch: ~20 of 2,599 intro frames; the slower render thread lags more). Fix
  `ffxiii2_renut_stable_dual_role_surfaces`: a header ever bound with a depth format is a bind-time copy in colour
  slots too. Side effect removed as well: the late read used to flip `g_surface_keys[6CB07E60]` between the colour and
  depth keys (`UnuseTarget` retires a target when its last user leaves).
- Class rule now: every header the game rewrites between roles is latched at bind time in every slot. Remaining live
  colour headers: those never bound as depth whose height never changes (the tiled scene's 6CB07EB0 and its
  format-only / tiling rebinds: copies of those split the scene in k207-k209 before the k229 origin check - worth
  re-testing now that tiling compares by origin; would make the rule simply "always latch").
- **Class closed (k241, CONFIRMED on the PC):** instead of a fourth case rule, `ffxiii2_renut_stable_all_surfaces` (default
  on) latches every heap header binding (copy per header and contents, R4 fixed: before, copies were keyed by contents
  only and the origin map was last-writer-wins; copies released with their header's destroy). The validator
  `ffxiii2_diag_late_headers` (bind-time contents per slot kind vs the render thread's read in GatherTargets and the
  resolve) found the fourth instance the case rules missed (6CB07E10, 320x180 vs 640x360, 270 / 10 s on the PC) and
  reads 0 with the key. `ffxiii2_diag_render_lag_us` makes the PC's render thread lag like the Switch's, so timing-only
  faults reproduce on the PC (l1 reproduced D-010's exit #4). Invariant now: **no renderer read of a surface header
  depends on when the render thread runs.** Remaining live reads of guest headers by the render thread: texture headers
  (fetch constants are latched in the device at SetTexture; BindTextureLocked also reads the header's Format words -
  OPEN, to measure the same way).
- Texture headers (checked 2026-11-07, code + k243d): BindTextureLocked reads the header but the device's fetch
  constant (latched at SetTexture, verified equal on both threads by the k205 delta check) wins whenever the two
  describe other data or differ in dwords 0/3/5 (`[flash]` lines); they may differ only in dword 4 (mip range, which
  SetTexture rewrites) and in fields `SameData` treats as the same memory. k243d battle: 0 `[flash]` lines (the old
  one per run was the shadow atlas, SA-1). So a late texture-header read cannot pick other data: the class is covered
  by the device latch. Making the device copy win always would be the plain console rule but can change mip-range
  pixels; left as is (pixel-identical rule).
- Detection method that found it (reusable): per-swap game-side draw chains (hook) vs per-frame render-side [rtlog]
  draw runs: a draw present in the chain but missing in [rtlog] is lost in the renderer; `draw exits` names the exit.
  Tools: tools/visual_debug/orphan_resolves.py, missing_draw_chains.py.

## 3. Open defects and their status after k238

| Id | Status | Next |
|---|---|---|
| D-003 band at y 336 (Switch; PC 0 of 510 frames) | the top 336 rows of the scene-colour resolve are not updated in banded frames; HUD banner scissor animation coincides | k238 on the Switch (C1 may share the cause): Frame Tap recording + band_scan |
| D-006 std::terminate / deaths when a Frame Tap stream attaches during load | CLOSED: Frame Tap's bug (user, 2026-11-07), not our build | none |
| SA-1 shadow atlas (1024x2048 view over both cascade resolves, built once from guest memory) | CONFIRMED, fixed k243d on the PC (`ffxiii2_renut_compose_resolves`: composed on the GPU from the resolves) | the user's look; Switch build made |

## 4. Regression matrix (what each change is checked against)

| Scene | Tool | Last result |
|---|---|---|
| Intro cutscene (face) | pass_counts + band dumps, flicker_offline.py / flicker_pgm.py (PC Python) | k238: 0 / 600 flicker frames; k240: see progress-log |
| Switch intro (D-010) | k239u procedure: [rtlog] + draw_chains, orphan_resolves.py; draw exits #4 | rc8: ~20 frames lost, exit #4 469 / 10 s; k240: pending (console asleep) |
| Chaos Bahamut battle | band dumps, flicker_offline.py | k238: 0 / 1,500 |
| New Bodhum (save 15, Serah) | the user | k229: fixed (user) |
| Switch battle band | Frame Tap + band_scan.py | k217 era: 1-5 % banded; k238: pending |
