# Options menu and DLC (FFXIII-2-Native-Switch, 2026-11-10)

## DLC (optional)
- Put the DLC package file(s) as they come from the Xbox 360 (LIVE/PIRS/CON packages of FINAL FANTASY XIII-2, title
  53510806, e.g. the "DLC Bundle Pack" 58D2695DF8A6B290B161A2CF02C0839B1873EEF353) into the `dlc` folder next to the
  game: `sdmc:/switch/ffxiii2-nx/dlc/` on the Switch, `dlc\` next to `renut.exe` on the PC.
- At the next start the game installs them (reNut upstream's installer, `app/src/renut_engine/dlc_install.cpp`, as in
  renut-nx's title-update-3.md): into the user data folder (`renut/0000000000000000/53510806/00000002/<package>/`).
  On the console the 845 MB bundle took 2 min 51 s at the first start (black screen meanwhile); later starts skip it
  (`dlc/installed.txt` and the installed header). PC: under a second.
- Without a `dlc` folder or package the game runs as before: the DLC is not required. Packages of other games or
  content types are left alone (logged).
- `dlc_enabled = false` (or "DLC: Off" in the options menu) shows the game no installed DLC and installs nothing; no
  file is deleted.
- Checked: PC (route, title -> battle) and console (New Bodhum): the game lists the package and opens its content
  (`[save] open ... (root 0000071)`); the title menu shows its "Downloadable content" entry (console vis2_p8c, French:
  "CONTENU TÉLÉCHARGEABLE").
- The console test tools (`backup_saves.py`, `restore_saves_ffns.py`) leave the installed DLC out of the save checks
  (folders `00000002`).

## Options menu: Start + Select (PC: F8)
Opens and closes over the game; meanwhile the game gets no controller input. D-pad moves, A changes a line, B goes
back. "Save" writes the lines' keys into `ffxiii2.toml` next to the game (other lines of that file stay as they are).
Lines marked "(restart)" take effect at the next start; the others at once.

| Line | Key(s) | Choices | When |
|---|---|---|---|
| Render resolution | `banjo_renut_dynamic_resolution`, `ffns_dynres_fixed_height` | 720p, 648p, 612p (default), 540p | restart |
| Shadows | `ffns_shadow_far_every` | every frame, far map every 2nd (default), every 4th | at once |
| Draw distance | `ffns_draw_distance` | full (default), 150 m, 100 m, 60 m | at once |
| MSAA | `msaa_level` | 2x (the game's), off | restart |
| SMAA | `banjo_renut_smaa` (after `banjo_renut_smaa_after_ps` = A2FBF0ABD9576D65) | off (default), on | at once |
| Upscaling | `ffns_fsr` (0 / 1 / 2), `ffns_fsr_sharpness` | bilinear (default), bilinear + sharpen (AMD RCAS, +1.3 ms GPU), FSR 1 (AMD EASU + RCAS, +6.2 ms GPU), FSR 1 sharp | at once |
| Level of detail (textures) | `ffns_texture_lod_bias` | original, +0.5, +1.0 | restart |
| FPS counter | `banjo_show_fps` | off, on | at once |
| Language | `user_language` | English, French, German, Spanish, Italian | restart |
| DLC | `dlc_enabled` | on, off | restart |
| Button layout | `input_xbox_layout` | Xbox positions, by letter | at once |
| Rumble | `input_rumble` | on, off | at once |

Not available yet (shown greyed): "Animate distant characters" (needs FF's per-character animation update found and
proven safe to run at half rate) and model level of detail.

Implementation: `app/src/renut_engine/overlays/options_menu_dialog.h`, `sdk/include/rex/ui/app_menu.h`,
`sdk/src/input/switch/switch_input_driver.cpp` (Start + Select: a lone Start or Select reaches the game 2 polls late, so
the combination does not also open the game's own menu), `sdk/src/ui/rex_app.cpp` (input gate). Patch scripts:
`tools/build/options_menu_patch.py`, `dlc_optional_patch.py`, `smaa_port_patch.py`, `msaa_level_patch.py`,
`texture_lod_patch.py`, `window_blocks_patch.py`.

## Console checks (2026-11-10, builds p8a-p8c; saves 0 changed after every run)
- menu1_p8a / menu2_p8c: Start + Select opens the menu over the field, D-pad / A change a line (draw distance 60 m),
  Start + Select closes it; FF's own menu does not open.
- vis1_p8b: SMAA on + MSAA off, picture right; GPU 28.2 ms vs 27.9 ms (defaults), render thread +2.6 ms.
- vis2_p8c: language French (title menu in French) and texture LOD +1.0, picture right.
- PC: msaa_level 1 and SMAA on, battle picture right.

## Upscaling (FSR 1), 2026-11-10
Only when the render resolution is under the shown size (the default 612p on the handheld's 720p screen): the scan-out
scales the frame. Bilinear: one filtered sample a pixel (as before). Bilinear + sharpen: then AMD FidelityFX RCAS.
FSR 1: AMD FidelityFX EASU (edge-adaptive upscaling) then RCAS. render/fsr.cpp, shaders/host/fsr_easu_ps.hlsl and
fsr_rcas_ps.hlsl (ports of SwitchWaker's MIT shaders, from AMD's MIT ffx_fsr1.h). Console S1b (GPU ms a frame, 120 s):
off 27.9, sharpen 29.2, FSR 1 34.1; fps unchanged (the main thread is the limit). Pictures right (vis4/vis5/vis6).

## Performance from renut-nx's docs (exact, on by default)
- ffns_window_blocks: the guest memory window on a 2 MB boundary (renut-nx 1.0.2's fix; the same misuse of
  virtmemFindAslr was in this SDK): console S1b ABBA -3.44 +- 0.60 ms a frame (~6 %).
- ffns_code_blocks (renut-nx 1.0.2's banjo_code_blocks, sdk/src/ui/switch_code_blocks.cpp): the NRO's code (49.6 MB) and
  read-only data (12.7 MB) moved to heap in line with 2 MB blocks at start; read from ffxiii2.toml itself (false turns it
  off). Console S1b ABBA: +0.81 fps (~4.3 %), about -2.1 ms a frame. Costs ~62 MB of heap.
- Boot cvars (katamari-nx 1.0.1): only the cvars the command line names are given to CLI11 (faster start).

## Saved builds
- 2026-11-10: "Single Joy-Con sideways" taken out of the menu (the key `input_joycon_sideways` still works in
  `ffxiii2.toml`, the menu just no longer shows it). Build saved in `out\builds\2026-11-10_p8d_menu_dlc\`.
