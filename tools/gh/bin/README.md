# Prebuilt PC tools (Windows x64)

`tools/gh/build_switch.ps1` builds only the Switch version, so it does not compile the PC tools of the code and shader
chain: they are here, built from this repository's own sources (no game data in any of them). They need the
Microsoft Visual C++ runtime (the build script installs it when it is missing).

| file | built from | used for | licence |
|---|---|---|---|
| `rexglue.exe`, `rexruntime.dll` | `sdk/` (the ReXGlue SDK fork), Release, LLVM clang 20 for the MSVC ABI | the code generator: PowerPC → C++ | BSD-3-Clause (`sdk/LICENSE`) and the licences of `sdk/thirdparty` |
| `XenosRecomp.exe` | `app/thirdparty/reblue-XenosRecomp` (reNut's XenosRecomp with this port's changes), Release | Xenos shaders → HLSL → SPIR-V (with DXC: `dxcompiler.dll` / `dxil.dll` from its `thirdparty/dxc-bin`) | MIT |
| `spirv-cross.exe` | [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) (main, 2026-11-04), MSYS2 clang64 (`tools/deko3d_build_tools.sh`) | SPIR-V → GLSL for deko3d | Apache-2.0 |
| `uam.exe` | `app/thirdparty/uam` (with the console's patches), MSYS2 clang64 (`tools/deko3d_build_tools.sh`) | GLSL → deko3d shader code (the shader cache and the host shaders) | zlib, MIT, MPL-2.0 (`app/thirdparty/uam`) |
| `glslangValidator.exe` | `sdk/thirdparty/glslang` (`tools/build_glslang.bat`) | the host shaders' SPIR-V check in the Switch build | BSD-3-Clause and others |

SHA-256:

```text
8703b6f6cee819422f7d263f82d68bc7d0b857d2d437bc012bea6d93b7e5ad8b  rexglue.exe
d7d57bb1120211b0bce6a4a10e862b0bdf513d7af771822de3f825422aeb52e3  rexruntime.dll
b930821d91ce5e3b6a13f341c846d88011cb12bb92374966478caa3c52f86874  XenosRecomp.exe
b21a4b5a3a90b8be8af0d4994c3452cd18cce99f6db096a6ae18b0baba2167d6  spirv-cross.exe
c7cb599e019756296c3051f9831b27ecd320944b6eaead7bd4dae744dae9cd1b  uam.exe
da47739b44ee71ada5213302d30d021336e56a0121527f9e70429e77fccbfbe0  glslangValidator.exe
```

Licences: `spirv-cross.exe` is Apache-2.0 (`LICENSE-Apache-2.0.txt` here); the others' licence files are next to their
sources in this repository (`sdk/LICENSE`, `app/thirdparty/reblue-XenosRecomp/LICENSE.md`, `app/thirdparty/uam`,
`sdk/thirdparty/glslang/LICENSE.txt`).

To build them yourself instead: the PC build of this tree (`tools/ffxiii2_pc_job.ps1`, Visual Studio 2022 Build Tools
+ LLVM clang 20) makes `rexglue.exe`, `rexruntime.dll` and `XenosRecomp.exe`; `tools/deko3d_build_tools.sh` (devkitPro's
MSYS2 with clang64) makes `spirv-cross.exe` and `uam.exe`; `tools/build_glslang.bat` makes `glslangValidator.exe`.
Then copy them here.
