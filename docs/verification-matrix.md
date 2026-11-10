# Final Fantasy XIII-2: Xbox 360 vs PC vs Switch verification matrix

Started 2026-11-05 (work order "Xbox 360-Accurate PC Port and Nintendo Switch Port"). Statuses: PASS / PARTIAL / FAIL /
UNTESTED / UNVERIFIED (no adequate Xbox 360 reference yet). Nothing is PASS against the Xbox 360 until compared with a
reference capture of the same state.

## Reference sources (in order of authority)
1. Original Xbox 360 hardware: **none available** in this project.
2. Project reference material: `Port/rightrender.png` (a frame of the battle tutorial, source: the YouTube video
   `Port/Final-Fantasy-XIII-2-SnapYT.App.mp4`, a capture of unknown settings: secondary), `Port/captura_013_0260s.png`
   (a PC capture, not a reference).
3. Xbox 360 emulator (catalog: `docs/reference-catalog.md`, session ref2): `Port/xenia_canary.exe` on `Port/Final Fantasy XIII-2 (Europe).iso` (`tools/xenia_ref.ps1`,
   captures every 15 s into `out/xenia_ref/<name>/`): **secondary evidence**, an emulator's picture, not the console's.
4. The SDK's emulated GPU on the PC (`banjo_renderer=emulated`, the game's own command stream): a third opinion only.

Game: Final Fantasy XIII-2, Europe, English (user_language 1), title update 6 (`default.xexp` 0.0.6.11). PC: Renut on
Vulkan, 1280x720. Switch: Renut on deko3d, 1280x720 handheld.

## Matrix

| Feature | Xbox 360 reference | PC (Renut, Vulkan) | Switch (Ryujinx / console rc1) | Difference | Status (PC vs 360) |
|---|---|---|---|---|---|
| Boot, logos | none yet | boots to the title (play8, tile*, verify*) | boots (r5+), console rc1 boots | - | UNVERIFIED |
| Title screen (logo, copyright, mode labels) | xenia ref2 011-013, 020-021 | clean (verify3/4) | rc2 (push constants off): clean in Ryujinx | - | PASS (xenia) |
| "Begin game in NORMAL MODE?" dialog | xenia ref2 022 (dark blue) | dark blue panel, text (verify4) | rc2: text right, panel teal/translucent | Switch-only (panel colour) | PASS (xenia); Switch FAIL |
| Main menu (New Game, DLC, Primer, Extras, Settings) | xenia ref2 012: DOWNLOADABLE CONTENT **greyed** | refpc1 cap 4: DOWNLOADABLE CONTENT **enabled** (dark text) | not compared | DLC item state | FAIL (open) |
| Extras menu | none yet | refpc1 caps 5-14 (icon row, "???") | not tested | - | UNTESTED |
| Settings menu | xenia ref2 019 | refpc1 pending | not tested | - | UNTESTED (PC) |
| Save creation screen | none yet | "New save data is required" dialog right (verify3 cap 12); one intermittent stall there (tile3) | garbled text / white-grey thumbnails on deko3d; right in g3 | Switch-only + PC stall | UNVERIFIED / PC PARTIAL |
| Save / load (actual files) | none yet | ff13-00..05 created (tile2/4) | "Save completed" seen (g4) | - | UNTESTED (load) |
| In-game Primer (Autosave page) | xenia ref2 028-029 (picture of Serah's room) | refpc1 pending | picture box garbage (r11, r13, console) | Switch at least | UNTESTED (PC) |
| Beginner's Primer (main menu) | xenia ref2 014-016 | refpc1 pending | not tested | - | UNTESTED |
| Opening FMV (Bink), pause screen | xenia ref2 031-038 (pause screen has the dot grid) | plays | plays; the console photo's dot grid is the pause screen | none found | PARTIAL (FMV frames not compared) |
| Opening cutscene (3D, subtitles) | none yet | renders | renders (r6, r11 30 fps) | - | UNVERIFIED |
| Battle tutorial (Chaos Bahamut) scene | xenia ref2 042-058; `rightrender.png` (video) | one scene, no seam; same brightness as xenia (18.8/27.6/32.4 vs 19-20/28-29/30-31) | renders | none vs xenia (video is brighter) | PASS (xenia) |
| Battle HUD (ATB gauge, commands, enemy name/HP) | xenia ref2 042-058 (gauge spans the screen) | as xenia (prim1 caps 12-15) | console rc1: text garbled (push constants; rc2 to be tested on the console) | Switch rc1 only | PASS (PC vs xenia) |
| Battle command menus | none yet | not compared | garbled text (console) | - | UNTESTED |
| Field movement, camera, interactions | none yet | not reached in scripted runs | console: played ~17 min (6-7 fps) | - | UNTESTED |
| Dialogue, subtitles | none yet | subtitles render ("I summon thee!") | render (r6, r12) | - | UNVERIFIED |
| Inventory, equipment, shops, maps | none yet | not reached | not reached | - | UNTESTED |
| Pause screen | none yet | not tested | not tested | - | UNTESTED |
| Audio (music, effects) | none yet | menu music (user confirmed), mix dumps | console: audio renderer up | - | UNVERIFIED |
| Input (pad, layout) | none yet | scripted presses work | console: user played | - | PARTIAL |
| Loading, transitions | none yet | load screens pass | console: multi-second stalls (run-time shader compiles) | Switch perf | UNVERIFIED |
| Performance | console only | 60 fps menus / PC | console rc1: 6-7 fps in the field (effect workers spin, 240 % CPU) | - | (Switch) FAIL |

## Open differences (each needs: test case, expected 360 behaviour, actual, evidence, root cause, fix, regression test)
1. deko3d text/HUD/render-to-texture garbage (Switch): **fixed in rc2** (`banjo_deko3d_push_constants` off; s_pc,
   r13). Regression test: Ryujinx `tools/ryujinx_run.ps1` with the normal toml, captures 75-185 s (save screen text and
   thumbnails). The enhancement's own bug: open (its float verify finds nothing; shared block suspected).
2. Battle brightness: **closed** (PC = xenia; the video frame is brighter).
2b. Dialog panels teal on the Switch (rc2), dark blue on xenia/PC: open (Switch-only; one of the remaining safe1 keys
   or a blend/format difference).
3. Save-screen stall (PC, once). Needs a reproduction.
4. Primer picture box (Switch; PC not compared).
5. FMV dot grid (console): **not a bug** (the game's pause screen, xenia ref2 038).
6. Main menu: DOWNLOADABLE CONTENT is enabled on the PC, greyed on xenia (ref2 012 vs refpc1 004). Ruled out: the
   sign-in state (both 1, signed in locally), XamUserCheckPrivilege (the PC denies all), the DLC enumeration (type 2:
   0 entries on the PC). Next: a XAM/XNet call trace of the title screen on the PC vs xenia.log, and whether a real
   offline Xbox 360 greys it (xenia is secondary evidence).
7. Scripted PC runs on the wall clock miss menus when the intro's length varies (refpc1 went to EXTRAS): the PC
   reference path needs state-waiting inputs or a hand-driven run like xenia's.
