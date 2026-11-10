# Rendering — invariants, known defects, pixel-identity procedure

## Invariants (carried over from the old project's audit; they are the contract for the native renderer)
1. **Bind-time latch:** a draw uses the surface header contents as they were at SetRenderTarget /
   SetDepthStencilSurface, never a later read (old fixes D-007 k211, D-008 k229, D-010 k240/k241 `stable_all`).
2. **Identity by guest semantics:** bindings refer to the same surface iff their origin header (+ contents) match;
   EDRAM targets keyed by base/pitch/MSAA/format; tiling check by origin (k229 — the split-screen seam fix). Copies
   keyed by header and contents (R4).
3. **EDRAM persists:** rewriting/dropping a header does not destroy a view's contents (R2 removed for copied headers).
4. **One EDRAM, many views:** writes through one view are visible through overlapping views. Old renderer: only clears
   (R3 gap) — model per case with evidence, never blind.
5. **Tiled passes:** predicated draws write only their tile; tiled resolves read their own tile; viewport clamp only
   when bound surfaces are the tiling surfaces (by origin).
6. **Resolve ordering:** a texture sampled after a resolve sees the whole resolved rect; textures laid over several
   resolve destinations are composed from them (SA-1, k243d).
7. **No visual change by default;** optional settings only with the user's approval.
8. **Diagnostics cost nothing when off.**
9. **VMX semantics are part of rendering correctness:** NaN/±0 behaviour of vmaxfp/vminfp decided which draws the game
   submitted (D-009). Every VMX float op must match the 360 bit-for-bit.

## Known defect state (last known from the old project; to be re-verified in this project)
| ID | State |
|---|---|
| D-003 Switch smoke band y≈336 | 0 / 2,062 frames on k243d; earlier 0.07-4.8 % → re-measure with a long played recording |
| D-006 game ends when Frame Tap attaches during load | open (lifecycle) |
| D-007, D-008, D-009, D-010 | fixed (classes C1, C2) |
| SA-1 shadow atlas | fixed on PC (k243d), Switch unverified |
| R3 EDRAM aliasing beyond clears | open gap |
| deko3d push constants break FF UI | worked around (off), root cause unknown |
| z-fighting, missing geometry | no open record → UNVERIFIED, include in the matrix |
| Teal dialog panels, Primer picture (Switch, 2026-11-05) | UNVERIFIED |

## Pixel-identity procedure (proposal)
- **Reference frames:** renderer read-back (`banjo_video_s` + `[capframe]`) of the current verified Renut build for a
  fixed scene list (title, intro shots, New Bodhum field, battle start/mid, smoke, shadows, split screen, a menu, a
  loading screen). Disputes go to the semantic ground truth: xenia / the 360 version.
- **Comparison:** bitwise per frame after aligning by game frame number; any difference → investigate before accepting.
  Tolerances only by the user's decision.
- **Capture tools:** Frame Tap / sys-botbase screenshots can show black or stale frames as artefacts (old k239) — use
  the renderer's own read-back; pictures never decide a bug without logs.

## GPU budget facts (old project, MEASURED)
Battle at 720p: 31.5 of 36.4 ms in 1280×720 targets (draws 26.4, colour resolves 2.6, MSAA resolves 0.5, depth
resolves 1.0 + 1.6 shadows). Heavy passes: PS 0938773DF7F7FA28 4 ms (opaque, cube map), FCF76CCFFD55FFDE smoke
2-6.5 ms (100-300 draws), 870B06B2245CD010 2.2 ms (full screen), opening scene AEAFDCD8198EDD3F 14 ms (32-tap radial
blur of RGBA16F).
