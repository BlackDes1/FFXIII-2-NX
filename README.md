# ffxiii2-nx — Final Fantasy XIII-2 for the Nintendo Switch

A native Nintendo Switch port of the Xbox 360 version of **Final Fantasy XIII-2** (Europe, title update 6).

It is not an emulator. The PowerPC code of the game's `default.xex` and its four zone modules is statically recompiled
to C++ with [ReXGlue](https://github.com/rexglue/rexglue-sdk) and compiled for the Switch's Cortex-A57; the game's
Direct3D calls are drawn on the Switch GPU's own API, deko3d, by reNut's Direct3D-level renderer.

> [!IMPORTANT]
> **This repository contains no game data and nothing made from it**: no executable, no recompiled code, no shaders,
> no textures, no prebuilt `.nro`. You need your own copy of the game, and the build makes everything on your PC
> from your files:
> - **required:** your disc image of **Final Fantasy XIII-2 (Europe)**, a plain `.iso` (XGD3, about 8.7 GB), or a
>   folder with the disc's files;
> - **required:** **title update 6** (0.0.6.11): the package `tu0000000b_00000000` (or a folder you extracted it into);
> - **optional:** the game's Xbox 360 **DLC** package(s). Without them the game runs as before, without DLC.
>
> Other regions or title updates are not supported (the function tables are for this executable; the build checks
> the SHA-256 of `default.xex` and `default.xexp`).

## Your files: SHA-256 checksums

The files this port was built and tested with. Check yours with PowerShell:
`Get-FileHash -Algorithm SHA256 "D:\Games\Final Fantasy XIII-2 (Europe).iso"`.

| file | size (bytes) | SHA-256 |
|---|---|---|
| `Final Fantasy XIII-2 (Europe).iso` — the disc image (required) | 8,738,846,720 | `3064232e4c97a58d70cc78ff39399d479200b383a4eb672d9a271d12dfe0115a` |
| `tu0000000b_00000000` — title update 6, 0.0.6.11 (required) | 32,821,248 | `f1b9dd3589a0d89199e8c713f105f6f4e4fcac8063e214614dc5f639248d497a` |
| `58D2695DF8A6B290B161A2CF02C0839B1873EEF353` — DLC package (optional) | 845,049,856 | `f15804b3c6b7e8e4924f89107416faccfdb813e5c042c2e704ba832c8539bb71` |

What the build itself checks (inside the files above; the build stops if they differ):

| file | from | SHA-256 |
|---|---|---|
| `default.xex` | the disc | `645745df61ea4466157dda317e0191424ccd47db46ee10f54c79db340dcdfe3b` |
| `default.xexp` | title update 6 | `8d48ccf8aabff2da2a1da3e4e4315fcb765007ac0c4d293880874f7464224029` |

An ISO made with another dumping tool (or trimmed) can have a different checksum and still work: what matters is
that its `default.xex` and the title update's `default.xexp` match the second table. A folder with the disc's files
works too.

## Build (Windows 10/11 x64)

### The easy way: the builder window

Download this repository (green **Code** button → *Download ZIP*, or `git clone`), then double-click
**`FFXIII-2-NX Builder.bat`**. Choose your disc image, the title update, optionally your DLC and an icon picture,
tick *Copy to the SD card* if your SD card is in the PC, and press **Build**. The window downloads the tools, builds
the game and shows the progress and the log; **Stop** stops it, and **Build** again continues where it stopped.

**Icon:** choose any picture — any size, any common format (PNG, JPG, WebP, BMP, GIF, TIFF, ICO, TGA, DDS, PSD, AVIF,
HEIC, ...; e.g. a Final Fantasy XIII-2 icon you downloaded). The build converts it to exactly what the `.nro` needs: a
256×256 JPEG. *Shape* decides what happens to a picture that is not square: **Fit** keeps the whole picture (black
bars), **Fill** crops it to a square, **Stretch** squeezes it. Transparent parts become black, photos are turned the
right way up, animated GIFs use their first frame. The window shows a preview of the result. A picture named `icon.*`
(e.g. `icon.png`) in the repository's folder is used by default. Your picture is only used for your own build and is
never part of this repository (`icon.*` is ignored by git); without one, the NRO has libnx's default icon.

### The command line

Download this repository (green **Code** button → *Download ZIP*, or `git clone`), then in its folder:

```bat
build_switch.cmd -Iso "D:\Games\Final Fantasy XIII-2 (Europe).iso" -Update "D:\Games\tu0000000b_00000000"
```

with the DLC:

```bat
build_switch.cmd -Iso "D:\Games\Final Fantasy XIII-2 (Europe).iso" -Update "D:\Games\tu0000000b_00000000" -Dlc "D:\Games\FFXIII-2 DLC"
```

or just **double-click `build_switch.cmd`**: it asks for the three paths (press Enter at the DLC question to build
without it).

**Only the Switch version is built** — no PC build, no Visual Studio, no LLVM. The script downloads and sets up
everything it needs by itself, into a tools folder (`.tools` in the repository, or `C:\ffxiii2nx_tools` when the
repository's path has spaces): CMake, Ninja, a portable Python with `xxhash` and `zstandard`, Zstandard, and
devkitPro (MSYS2 + devkitPro's pacman: `switch-dev`, `deko3d`, `switch-zlib`, `switch-libzstd`) when no devkitPro is
installed — an installed `C:\devkitPro` is used instead (missing Switch packages are added with its own pacman). The
Microsoft Visual C++ runtime is installed when it is missing (Windows asks for administrator rights once). The PC
tools of the shader chain are prebuilt in [`tools/gh/bin`](tools/gh/bin/README.md).

| step | what it does |
|---|---|
| `tools`   | downloads and installs the tools above (nothing outside the tools folder, except the VC++ runtime if missing) |
| `extract` | your ISO (`tools/gh/iso_tool.py`) and the title update (`tools/stfs_extract.py`); the SHA-256 check |
| `codegen` | the PowerPC code → C++ (ReXGlue per module, then the optimising post passes) |
| `shaders` | every shader container of your game files (scanned straight from the ISO), the title update and the DLC → SPIR-V → GLSL for deko3d (`tools/switch_shaders.ps1`) |
| `cache`   | the **shader cache**: every pipeline of the recorded play-throughs (`app/config/ffxiii2/pipeline_lists`, shader hashes and render states only) plus every shader's generic variant, compiled for the Switch GPU with the console's uam |
| `switch`  | the NRO (devkitA64, link-time optimisation + the profile-guided optimisation profile in `pgo/`) |
| `package` | the SD card folder |
| `sdcard`  | (with `-SdCard`) copies it to your SD card |
| `cleanup` | deletes everything the build made except the SD folder: the generated code, the build folders, the shader work files, the extracted modules — and the downloaded tools (keep them with `-KeepTools`) |

It takes about 2–3 hours the first time. If it stops (no internet, disk full, closed window), run the same command
again: it continues after the last finished step (`-From <step>` redoes from a step).

### Disk space

About **15 GB free** (the 8.7 GB of game files in it stay: they go on your SD card), **about 7 GB with
`-NoGameFiles`** (your game files are already on the SD card), plus about 2.5 GB while devkitPro is installed locally.
At the end only the SD folder is left (about 9 GB with the game files, 150 MB without).

### Options

| option | |
|---|---|
| `-Dlc <file or folder>` | your DLC package(s): copied to the SD folder's `dlc\` and their shaders included |
| `-Icon <picture>` | the NRO's icon: any picture in any size and common format, converted to the 256×256 JPEG the NRO needs (default: `icon.*` in the repository's folder) |
| `-IconMode fit\|fill\|stretch` | for a picture that is not square: whole picture with black bars (default), cropped to a square, or squeezed |
| `-SdCard <drive>` | copy the finished folder to your SD card (e.g. `E:`); nothing on it is deleted, an `ffxiii2.toml` there keeps your settings |
| `-NoGameFiles` | the game files are not put into the SD folder (they are already on your SD card); the disc is read without extracting it |
| `-Out <folder>` | where the SD folder goes (default `SD\` in the repository) |
| `-Jobs N` | compile jobs (default: the cores, at most half the RAM in GB) |
| `-KeepTools`, `-KeepWork` | keep the downloaded tools / the build folders after the build |
| `-NoPgo` | build without the PGO profile (slower game) |
| `-DevkitPro <folder>`, `-Tools <folder>`, `-Work <folder>` | an installed devkitPro / where the tools and the temporary files go |

## Install on the Switch

Copy the folder `SD\switch` to the root of the SD card:

```text
/switch/ffxiii2-nx/
  ffxiii2.nro          the game
  ffxiii2.toml         your settings (language, button layout); the options menu writes it too
  deko3d_glsl.bin      the game's shaders, translated from your files
  deko3d_shaders.bin   the shader cache (the console adds what it compiles itself as you play)
  game_root/           your disc's files (default.xex, alba_data, ...)
  update/              title update 6's files (default.xexp, the z*.xexp, white_patch_*)
  dlc/                 (optional) your DLC packages: installed by the game at its next start (about 3 minutes, black screen)
```

Start it from the Homebrew Menu in **title-takeover mode** (hold R while starting any game), not from the album
applet: the game needs the full memory. Atmosphère is required. Logs go to `/switch/ffxiii2-nx/logs`.

## In the game: the options menu

Press **Start + Select** together at any time to open or close the options menu (on the PC build: F8). While it is
open the game gets no controller input. **D-pad** moves between lines, **A** changes the selected line, **B** goes back,
**Save** writes your choices into `/switch/ffxiii2-nx/ffxiii2.toml` (other lines of that file stay as they are).
Lines marked *(restart)* take effect the next time you start the game; all others change at once.

### Graphics

| Line | Choices | What it does |
|---|---|---|
| **Render resolution** *(restart)* | 720p (native), 648p, **612p** (default), 540p | The height of the 3D image (the HUD and menus always stay at 720p). Lower = less GPU work and a softer picture. It stays fixed for the whole session (switching it while playing broke the battle picture once in testing). |
| **Shadows** | Every frame (original), **far map every 2nd frame** (default), far map every 4th frame | How often the large, far shadow map is redrawn. Every 2nd frame saves about 1.3 ms per frame; far shadows then trail fast-moving objects by a frame. |
| **Draw distance** | **Full** (original, default), 150 m, 100 m, 60 m | Placed objects (people, props) farther from the camera than this are not drawn. The town and landscape themselves are always drawn. |
| **Level of detail (textures)** *(restart)* | **Original**, lower (+0.5), lowest (+1.0) | Distant textures switch to their smaller versions sooner: less GPU memory traffic, blurrier distance. |
| **MSAA** *(restart)* | **2x** (the game's own), off | The game's 2x multisampling of the 3D image. Off: rougher edges, less GPU time and memory. |
| **SMAA** | **Off**, on | Edge smoothing (SMAA 1x) of the finished 3D image before the HUD is drawn: smoother edges for a little GPU time. Works well together with MSAA off. |
| **Upscaling** | **Bilinear** (default), bilinear + sharpen, FSR 1, FSR 1 sharp | How the picture is scaled to the screen when the render resolution is below 720p. *Sharpen* adds AMD FidelityFX RCAS sharpening (about +1.3 ms GPU per frame). *FSR 1* is AMD FidelityFX Super Resolution 1 (edge-adaptive EASU upscaling + RCAS, about +6.2 ms GPU per frame: sharper picture, can cost frames in GPU-heavy scenes). *FSR 1 sharp* uses stronger sharpening. Has no effect at 720p. |
| **FPS counter** | **Off**, on | Shows frames per second in the top-left corner. |
| Animate distant characters, Level of detail (models) | — | Shown greyed: not available yet. |

### System

| Line | Choices | What it does |
|---|---|---|
| **CPU clock** | **1020 MHz** (stock, default), 1224 MHz, 1428 MHz | Raises the Switch CPU clock while the game runs, set by the game itself (no sys-clk needed). The console's own clock comes back when the game closes. GPU (460.8 MHz) and RAM (1331.2 MHz) stay at Nintendo's own handheld values. **1224 and 1428 MHz are an overclock — read the warning below.** |

### Game and controls

| Line | Choices | What it does |
|---|---|---|
| **Language** *(restart)* | English, French, German, Spanish, Italian | The game's text and menu language (the European disc's languages). |
| **DLC** *(restart)* | **On**, off | Uses the DLC packages in `/switch/ffxiii2-nx/dlc/` (installed by the game at its first start, about 3 minutes with a black screen). Off: the game sees no DLC. Without DLC packages this changes nothing. |
| **Button layout** | Xbox positions, **by letter** | *Xbox positions*: the bottom face button confirms, as on the Xbox 360. *By letter*: the Switch's A button confirms. |
| **Rumble** | **On**, off | Controller vibration. |

Settings that are not in the menu (e.g. button remapping, single Joy-Con held sideways) can be changed directly in
`ffxiii2.toml`; the file explains each key.

**Performance** (console, handheld, New Bodhum field): about 25 fps at the stock CPU clock and about 29 fps at
1428 MHz; the opening battle holds 30 fps at 1428 MHz. Details in [`docs/native/performance.md`](docs/native/performance.md)
and [`docs/native/30fps-steps123-2026-11-10.md`](docs/native/30fps-steps123-2026-11-10.md).

## ⚠️ Overclocking: risks and disclaimer

The **CPU clock** option runs the Switch's CPU above the clock Nintendo uses for games (1020 MHz). It is **off by
default** and only active while you choose 1224 or 1428 MHz. Running a console out of specification can cause:

- higher temperature and louder fan, faster battery drain, and shorter battery life over time;
- instability (crashes, freezes) on some consoles, and in the worst case **damage to the console, the SD card or your
  save data**;
- behaviour that differs between console revisions; it was tested on one console only.

Use it at your own risk. If the game becomes unstable, set **CPU clock** back to **1020 MHz (stock)**. Do not combine
it with other overclocking tools (sys-clk and the like) at the same time.

**Disclaimer: this software is provided "as is", without warranty of any kind (see the GPL-3.0 `LICENSE`). The
author(s) and contributors take no responsibility for any damage to your console, SD card, save data, PC or anything
else, or for any other loss, caused by using this project, its build scripts, the overclock option, custom firmware
or homebrew in general. You use all of it entirely at your own risk.**

## State

This is the FFXIII-2-Native-Switch tree of 2026-11-10 (build p11): the register-argument codegen (P1) with its post
passes, the XMA decoder on core 3, promoted indirect calls, window and code blocks on 2 MB boundaries, the options
menu, optional DLC, FSR 1, the CPU clock toggle, and link-time + profile-guided optimisation (the profile in `pgo/` was
recorded on the console in New Bodhum and the opening battle). The notes are in [`docs/native/`](docs/native/)
(progress log, performance, rendering, options menu and DLC); the older notes of the k263 tree stay in `docs/`.

## Layout

- `FFXIII-2-NX Builder.bat`, `build_switch.cmd`, `tools/gh/` — the builder window (`builder_gui.ps1`), the one-command
  Switch build (`build_switch.ps1`, `iso_tool.py`) and the prebuilt PC tools in `bin/`.
- `app/` — the game project: hooks, the renderer (`src/renut_engine`), the options menu, the codegen settings
  (`renut_config.toml`, `renut_manifest.toml`, `config/`), the native copies (`src/cpu/hot`); `app/generated*` is made
  by the build.
- `sdk/` — the ReXGlue SDK fork with the Switch platform layer, its third-party sources in `sdk/thirdparty`.
- `pgo/` — the PGO profile (GCC's branch counts of this port's own compiled code; no game data).
- `tools/` — the build, shader, packaging, profiling and test tools.
- `dist/switch/ffxiii2-nx/` — test and measurement configurations.
- `docs/` — notes, investigations, the progress logs.

## Credits and licence

This port stands on the work of many people. Thank you to:

- **masterspike52 and reNut** — reNut's Direct3D-level renderer (reuse permitted by its author).
- **Tom Clay and the ReXGlue SDK**, derived from **Xenia** (Ben Vanik and the Xenia contributors) — the recompiler and
  runtime.
- **ChanseyIsTheBest** — katamari-nx (this port's base), renut-nx / banjo-nx (the Switch renderer, the DLC installer,
  the 2 MB code and memory blocks), sa2-nx, futari-nx, renut-nx-webinstaller, bejeweled3-nx and the Peggle 2 / Sonic &
  All-Stars Racing Transformed ports.
- **StevensND** — nfsmw-nx (the Switch layer of the SDK, the profiling and PGO method); **madelrandel-blip** —
  NFSMW Recompiled; **NebadasSwifty** — masseffect-nx.
- **hedge-dev and contributors** — XenosRecomp (the shader translator), XenonRecomp, Unleashed Recompiled and its Switch
  port (the profiler, plume's Switch patches).
- **Pulpparty, depende3000 and the SwitchWaker contributors** — the FSR shader ports (MIT); **AMD** — FidelityFX Super
  Resolution 1.0 (MIT).
- **Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro and Diego Gutierrez** — SMAA (MIT).
- **devkitPro and switchbrew** — devkitA64, libnx, deko3d, uam, switch-tools; **Atmosphère** and the Homebrew Menu.
- **The library authors** in `sdk/thirdparty` and `app/thirdparty`: FFmpeg, libmspack, glslang, SPIRV-Tools,
  SPIRV-Cross, the Vulkan headers, Vulkan Memory Allocator, plume, Dear ImGui, fmt, spdlog, xxHash, zstd, SIMDe,
  toml++, o1heap, utfcpp, CLI11, smol-v, and Microsoft's DirectX Shader Compiler.
- **The builder's tools**: CMake, Ninja, Python, Pillow, MSYS2; **Ryujinx**, **Xenia Canary** and **sys-botbase** for
  testing.

Every component, what it is used for and its licence: [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md). This
repository is GPL-3.0 (`LICENSE`), with the SDK's Switch layer under BSD-3-Clause and the shader translator changes
under MIT as noted there. Final Fantasy XIII-2 is © Square Enix; this project is not affiliated with or endorsed by
Square Enix, Microsoft or Nintendo, and contains no game data.
