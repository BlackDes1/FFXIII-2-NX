@echo off
rem ffxiii2-nx: the Windows host toolchain (katamari-nx's pc_env.bat):
rem   - MSVC headers/libs + Windows SDK from the VS 2022 Build Tools (vcvars64)
rem   - the VS-bundled CMake and Ninja (never the MSYS2 cmake: it mangles Windows paths)
rem   - LLVM clang 20 driving the MSVC ABI, linked with lld-link
rem Usage: call tools\pc_env.bat, then run cmake with %REX_CMAKE_COMPILERS%.
rem Overrides (tools\gh\build_from_iso.ps1 sets them; the defaults are the original PC's):
rem   FFX_VSROOT  the Visual Studio (Build Tools) folder            default: VS 2022 Build Tools
rem   FFX_LLVM    the LLVM folder (with bin\clang.exe)               default: Works\_tools\llvm
rem   FFX_PYTHON  python.exe                                         default: C:\Python314\python.exe, else PATH
if defined FFX_VSROOT (set "VSROOT=%FFX_VSROOT%") else (set "VSROOT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools")
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
if defined FFX_PYTHON (
  for %%P in ("%FFX_PYTHON%") do set "PATH=%%~dpP;%PATH%"
) else (
  if exist C:\Python314\python.exe set "PATH=C:\Python314;%PATH%"
)
if defined FFX_LLVM (set "LLVM_BIN=%FFX_LLVM%\bin") else (set "LLVM_BIN=%~dp0..\..\_tools\llvm\bin")
for %%I in ("%LLVM_BIN%") do set "LLVM_BIN=%%~fI"
set "LLVM_BIN=%LLVM_BIN:\=/%"
if defined FFX_PYTHON (set "PY=%FFX_PYTHON%" & goto :py_found)
for /f "delims=" %%P in ('where python') do (set "PY=%%P" & goto :py_found)
:py_found
set "PY=%PY:\=/%"
rem Quoted: a path with a space (e.g. "Neuer Ordner") split the compiler path at the first configure.
set REX_CMAKE_COMPILERS="-DCMAKE_C_COMPILER=%LLVM_BIN%/clang.exe" "-DCMAKE_CXX_COMPILER=%LLVM_BIN%/clang++.exe" -DCMAKE_C_COMPILER_TARGET=x86_64-pc-windows-msvc -DCMAKE_CXX_COMPILER_TARGET=x86_64-pc-windows-msvc -DCMAKE_LINKER_TYPE=LLD "-DCMAKE_RC_COMPILER=%LLVM_BIN%/llvm-rc.exe" "-DCMAKE_AR=%LLVM_BIN%/llvm-ar.exe" "-DCMAKE_C_FLAGS=-march=x86-64-v2" "-DCMAKE_CXX_FLAGS=-march=x86-64-v2" "-DPYTHON_EXECUTABLE=%PY%" "-DPython3_EXECUTABLE=%PY%"
rem The code generator writes the loaded game image here (tools\post_codegen.py: ro_constants.py and the entry modes
rem of flush_mode_flow.py read it). Game data: never committed or distributed.
set "REXGLUE_DUMP_IMAGE=%~dp0..\out\renut_image.bin"
