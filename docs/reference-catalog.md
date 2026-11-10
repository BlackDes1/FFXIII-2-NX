# Xbox 360 reference catalog (xenia_canary, secondary evidence)

Session **ref2**, 2026-11-05 09:51-10:06 (PC local time). `tools/xenia_ref.ps1 -Name ref2 -Seconds 1500 -Every 15`, run from
the job runner; driven by hand through computer use (held keys: A = Enter, B = N, Start = X, Back = Z, D-pad = arrows;
`--keyboard_mode=1` on xenia's command line, the user's xenia config not edited).

Conditions: `Port/xenia_canary.exe` (Canary experimental build of Mar 15 2025, D3D12), disc image `Port/Final Fantasy XIII-2
(Europe).iso`, xenia's `apply_title_update = true` (whether TU6 is installed in xenia's content folder was not checked:
the title screen and menus show no version), user language 1 (English), xenia profile "ede", draw resolution scale 1,
window maximised on the 2560x1440 desktop: captures 2560x1349 (client area), the 16:9 game picture 2398x1349 at x = 81,
normalised to 1280x720 in `out/xenia_ref/ref2/norm/` (crop + Lanczos). Contact sheet: `out/xenia_ref/ref2/contact.jpg`.
Captures are game data (screens of the game): kept in `out/` (not in git).

| Capture (s) | State |
|---|---|
| 001 (15) | "Press any button to continue." |
| 002-010 (30-150) | intro movie with credits (city with green rings, Noel, Yeul, Caius), not skipped by A/Start until 165 s |
| 011-013 (165-195) | title, main menu (NEW GAME highlighted, DOWNLOADABLE CONTENT greyed, BEGINNER'S PRIMER, EXTRAS, SETTINGS, copyright line) |
| 014 (210) | Beginner's Primer chapter list (Prologue .. Chapter 13) |
| 015-016 (225-240) | Beginner's Primer, Prologue page |
| 017-018 (255-270) | back to the main menu |
| 019 (285) | Settings (Game Settings: Camera, Camera Sensitivity, Brightness 0, Magnify Font, subtitles, Help Messages, Battle Difficulty NORMAL, ...) |
| 020-021 (300-316) | main menu, NORMAL MODE / EASY MODE |
| 022 (330) | "Begin game in NORMAL MODE?" dialog (dark blue panel) |
| 023-024 (345-360) | "New save data is required to play FINAL FANTASY XIII-2." |
| 025 (375) | Save: Xbox 360 Hard Drive, New Save Data (one slot), Total hours played 000:00:00, Total fragments 000/160 |
| 026 (390) | "Create new save data?" Yes/No (dark blue panel) |
| 027 (405) | "Save completed.", Save Data 01 |
| 028-029 (420-435) | in-game Primer, PAGE 1/1, Autosave: picture of Serah's room with Mog and the autosave icon; text with the START icon |
| 030 (450) | FINAL FANTASY XIII-2 logo, NOW LOADING |
| 031-036 (465-540) | opening movie (Noel falling, Lightning, Valhalla) |
| 037 (555) | real-time cutscene with a subtitle |
| 038 (570) | PAUSED over the movie (dot-grid pattern, BACK Skip / START Resume) |
| 039 (585) | loading |
| 040-041 (600-615) | Odin across the water (real-time) |
| 042-044 (630-660) | battle tutorial: "Learn about the battle basics?" Take tutorial / Skip; HUD (COMMANDS: Auto-battle, Abilities, Items; ATB gauge across the screen; Lightning HP 3800) |
| 045-046 (675-690) | "Time does not stop during battle. The ATB gauge charges continuously over time." |
| 047-050 (705-750) | "You can queue up multiple commands ..."; the gauge segments |
| 051 (765) | "Lightning Strike has been set twice ..."; two Lightning Strike slots; TARGET Chaos Bahamut |
| 052-054 (780-810) | "Select a target ..."; Chaos Bahamut name and stagger gauge |
| 055-058 (825-870) | attacks, HP 3420 |

## First comparisons (2026-11-05)
- Battle brightness, the same tutorial state: PC (prim1 cap 15) mean RGB 18.8/27.6/32.4; xenia ref_044 19.2/28.9/30.6,
  ref_046 20.0/28.1/30.1. **Equal within ~1-2 levels**: the darker look than `Port/rightrender.png` (a YouTube frame) is
  the video's, not the renderer's.
- Battle HUD: the ATB gauge spans the screen on the Xbox 360 (xenia) as on the PC: correct.
- Dialog panels ("Begin game", "Create new save data?"): dark blue on xenia and the PC; teal on the Switch (rc2 build in
  Ryujinx): a remaining Switch-only difference.
- FMV pause screen: the dot-grid pattern is the game's own (xenia 038); the console photo's grid was this screen: not a bug.
- In-game Primer picture: a screenshot of Serah's room (xenia 028); the Switch shows garbage there; the PC comparison is
  run refpc1 (pending).
