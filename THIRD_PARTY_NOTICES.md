# Third-party notices

This port is built on the work of other projects. Their licenses apply to their code and to the parts of this repository
derived from it; the code taken from them keeps its authors' copyright. New code written for this port (FFXIII-2 NX:
the native Switch port of Final Fantasy XIII-2 and its builder) is copyright (c) 2026 BlackDes1 and is under the license
of the folder it is in (see below).

## Projects this port is derived from

| Project | Used for | License |
|---|---|---|
| [katamari-nx](https://github.com/ChanseyIsTheBest/katamari-nx) by ChanseyIsTheBest | This port's base tree (the k263 tree), its codegen post passes, the boot-cvar fix (1.0.1) | GPL-3.0 |
| [renut-nx](https://github.com/ChanseyIsTheBest/renut-nx) (banjo-nx: Banjo-Kazooie: Nuts & Bolts) by ChanseyIsTheBest | The tree this port started from: the SDK fork, the app, the tools, the renderer's adaptation, plume's deko3d back end, the shader passes, the DLC installer; from 1.0.2 the guest memory window and the NRO's code on 2 MB blocks (`ffns_window_blocks`, `switch_code_blocks.cpp`) | GPL-3.0 |
| [UnleashedRecomp-NX](https://github.com/hedge-dev/UnleashedRecomp) (the Switch port of Unleashed Recompiled) | The CPU/GPU profiler (`app/src/profiler`), plume's Switch patches (`thirdparty/plume`), the CPU-boost approach | GPL-3.0 |
| [sa2-nx](https://github.com/ChanseyIsTheBest/sa2-nx), [futari-nx](https://github.com/ChanseyIsTheBest/futari-nx), Peggle 2's and Sonic & All-Stars Racing Transformed's ports by ChanseyIsTheBest | CPU rounds, built-in settings, release shader files, fixes | GPL-3.0 |
| [masseffect-nx](https://github.com/nebadasSwifty/masseffect-nx) by NebadasSwifty | The structure of `app/`, `app/src/me` | GPL-3.0 |
| [nfsmw-nx](https://github.com/StevensND/nfsmw-nx) by StevensND | The Horizon (Switch) layer of the SDK, the profiling and PGO method, scripts and the toolchain file | GPL-3.0; the SDK parts BSD-3-Clause |
| [NFSMW Recompiled](https://github.com/madelrandel-blip/NFSMW-Recompiled) by madelrandel-blip | The recompilation project the ports above started from | GPL-3.0 |
| [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) by Tom Clay, with portions from [Xenia](https://xenia.jp) (Ben Vanik and the Xenia contributors) | `sdk/` | BSD-3-Clause (`sdk/LICENSE`) |
| [XenosRecomp](https://github.com/hedge-dev/XenosRecomp) by hedge-dev and contributors, with the changes of the ports above and this one | `shaders/`, `app/thirdparty/reblue-XenosRecomp` | MIT |

The Horizon (Switch) layer added to `sdk/` is under the SDK's BSD-3-Clause license, and the changes in `shaders/` and
`app/thirdparty/reblue-XenosRecomp` under XenosRecomp's MIT license, so they can be reused by other ports. The rest of
this repository (`app/`, `tools/`, `docs/`) is GPL-3.0 (`LICENSE`).

## The deko3d renderer (app/src/renut_engine, app/thirdparty)

- **reNut** (github.com/masterspike52/reNut, by reNut and masterspike52): the Direct3D-level renderer in
  `app/src/renut_engine/gpu`, through banjo-nx's adaptation. reNut has no licence file; its author permits the user's
  ports to reuse it.
- **plume** (`app/thirdparty/plume`): MIT; its deko3d back end (`plume_deko3d*`) is banjo-nx's, under the same licence.
- **uam** (`app/thirdparty/uam`): zlib (uam), MIT (the Mesa files it imports), MPL-2.0 (SwitchWakerHD's patches).
- **reblue-XenosRecomp** (`app/thirdparty/reblue-XenosRecomp`): MIT, with its own third-party folder (zstd, xxHash,
  smol-v, DXC) under their licences.
- **Dear ImGui** (`app/thirdparty/imgui`, the options menu and the overlays): MIT.
- **SMAA** (`app/thirdparty/smaa`, the options menu's SMAA): MIT, Copyright (C) 2013 Jorge Jimenez, Jose I. Echevarria,
  Belen Masia, Fernando Navarro and Diego Gutierrez (`app/thirdparty/smaa/LICENSE.txt`).
- **deko3d** (devkitPro): zlib, linked from devkitPro's portlibs.

## Options-menu upscaling (FSR 1)

- **AMD FidelityFX Super Resolution 1.0** (`ffx_fsr1.h`, Copyright (c) 2021 Advanced Micro Devices, Inc., MIT): the EASU
  and RCAS algorithms in `app/src/renut_engine/gpu/shaders/host/fsr_easu_ps.hlsl` / `fsr_rcas_ps.hlsl`.
- **SwitchWaker** (MIT, Copyright (c) 2026 Pulpparty and depende3000, and the SwitchWaker contributors): those two
  shaders are ports of its deko3d WGSL ports of FSR 1 (`dk_fsr_easu.wgsl`, `dk_fsr_rcas.wgsl`).
- Both licence texts: `app/src/renut_engine/gpu/shaders/host/LICENSE-FSR.txt`.

## Prebuilt PC tools (tools/gh/bin)

Built from this repository's sources (`sdk/`, `app/thirdparty/reblue-XenosRecomp`, `app/thirdparty/uam`,
`sdk/thirdparty/glslang`) and from SPIRV-Cross (Apache-2.0); each under the licence of its sources
(`tools/gh/bin/README.md`; SPIRV-Cross's Apache-2.0 text: `tools/gh/bin/LICENSE-Apache-2.0.txt`). The DirectX
Shader Compiler binaries in `app/thirdparty/reblue-XenosRecomp/thirdparty/dxc-bin` are Microsoft's (`NOTICE.md` there).

## What the builder downloads (not part of this repository)

`tools/gh/build_switch.ps1` downloads these from their official sites when they are missing, each under its own licence:

| Component | Used for | Licence |
|---|---|---|
| [CMake](https://cmake.org) 3.31.6 (Kitware) | the Switch build | BSD-3-Clause |
| [Ninja](https://ninja-build.org) 1.12.1 | the Switch build | Apache-2.0 |
| [Python](https://www.python.org) 3.14.8 (portable, from nuget.org) | the build tools | PSF License |
| Python packages [xxhash](https://github.com/ifduyue/python-xxhash), [zstandard](https://github.com/indygreg/python-zstandard), [Pillow](https://python-pillow.org), [pillow-heif](https://github.com/bigcat88/pillow_heif) (optional: HEIC icons) | shader hashes, shader packages, the icon converter | BSD-2-Clause, BSD-3-Clause, MIT-CMU, BSD-3-Clause (bundles libheif: LGPL-3.0) |
| [Zstandard](https://github.com/facebook/zstd) 1.5.7 (zstd.exe) | the shader package dictionary | BSD-3-Clause / GPL-2.0 |
| [MSYS2](https://www.msys2.org) and [devkitPro](https://devkitpro.org) (devkitA64 / GCC, libnx, deko3d, switch-tools, switch-zlib, switch-libzstd, make) | the Switch toolchain and libraries | their own licences (GCC: GPL-3.0 with the runtime library exception; libnx: ISC; deko3d: zlib) |
| Microsoft Visual C++ Redistributable | the prebuilt PC tools | Microsoft's redistribution terms |

## Methods and references

Ideas and methods taken from other projects, without their code: [XenonRecomp](https://github.com/hedge-dev/XenonRecomp)
(registers as locals, the "largest gains" codegen options), [renut-nx-webinstaller](https://github.com/ChanseyIsTheBest/renut-nx-webinstaller)
(every shader container of the game files instead of captured runs), [bejeweled3-nx](https://github.com/ChanseyIsTheBest/bejeweled3-nx)
(the optimising-functions policy), SwitchWaker's documentation (renderer lessons), AMD's FidelityFX documentation.

## Development and testing tools (not part of this repository)

[Ryujinx](https://github.com/Ryujinx/Ryujinx) (MIT) and [Xenia Canary](https://github.com/xenia-canary/xenia-canary)
(BSD-3-Clause) for checks on the PC; [sys-botbase](https://github.com/olliz0r/sys-botbase) for the console test
automation; [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere) (GPL-2.0) and the
[Homebrew Menu](https://github.com/switchbrew/nx-hbmenu) (ISC), which the game needs on the console.

## Libraries

Libraries used by the SDK, the NRO and the code generator, in `sdk/thirdparty`, each with its own license file:

| Library | License |
|---|---|
| [FFmpeg](https://ffmpeg.org) (libavcodec, libavutil: the XMA, WMA and WMV decoders) | LGPL-2.1-or-later |
| [libmspack](https://www.cabextract.org.uk/libmspack/) by Stuart Caie (LZX decompression) | LGPL-2.1 |
| [glslang](https://github.com/KhronosGroup/glslang) | BSD-3-Clause and others |
| [SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools) | Apache-2.0 |
| [SPIRV-Headers](https://github.com/KhronosGroup/SPIRV-Headers), [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | MIT, Apache-2.0 |
| [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | MIT |
| [fmt](https://github.com/fmtlib/fmt), [spdlog](https://github.com/gabime/spdlog) | MIT |
| [xxHash](https://github.com/Cyan4973/xxHash) | BSD-2-Clause |
| [SIMDe](https://github.com/simd-everywhere/simde) | MIT |
| [toml++](https://github.com/marzer/tomlplusplus) | MIT |
| [o1heap](https://github.com/pavel-kirienko/o1heap) | MIT |
| [utfcpp](https://github.com/nemtrif/utfcpp) | BSL-1.0 |
| [CLI11](https://github.com/CLIUtils/CLI11) | BSD-3-Clause |
| [libnx](https://github.com/switchbrew/libnx) and the devkitA64 runtime (newlib, libstdc++) | ISC; newlib and GCC runtime licenses |
| zlib and Zstandard, from devkitPro's Switch portlibs | zlib License; BSD-3-Clause |

Only for builds on a computer or for the tests, never in the Switch NRO: SDL3 (zlib), the Vulkan loader (Apache-2.0),
Catch2 (BSL-1.0).

### FFmpeg and libmspack (LGPL)

Everything needed to rebuild and relink the NRO with a modified version of either library is in this repository and in
[docs/building.md](docs/building.md).

## The installer page

The installer page is its own repository, [katamari-nx-installer](https://github.com/ChanseyIsTheBest/katamari-nx-installer).
Its WebAssembly shader translator is built from `app/thirdparty/reblue-XenosRecomp` and `tools/renut_hlsl_frontend.cpp` of
this repository (with [Emscripten](https://emscripten.org): MIT and the University of Illinois/NCSA license); its other
components are listed in its `NOTICES.md`.

## Trademarks

Final Fantasy XIII-2 is © Square Enix. Nintendo Switch is a trademark of Nintendo. Xbox 360 is a trademark of
Microsoft. This project is not affiliated with or endorsed by any of them, and contains no game data.
