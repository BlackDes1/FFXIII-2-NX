@echo off
rem ffxiii2-nx: the code generator for Final Fantasy XIII-2 (default.xex + the four zone DLLs), then the module lists
rem and katamari-nx's post-codegen passes.
rem   1. builds the host rexglue.exe from this tree's sdk\ into out\host when sdk\out\win-amd64\rexglue.exe is missing;
rem   2. one codegen run per zone module (--target zXXXX): all four load at 0x88800000, and one run over all of them maps
rem      them over each other (bogus "target not in any function"; docs/ffxiii2-nx.md);
rem   3. tools\ffxiii2_post_codegen_modules.py: all four modules in the registry and the CMake targets, the build's own
rem      codegen step made a no-op (it would run all four at once);
rem   4. tools\post_codegen.py (katamari-nx's optimised-codegen passes) on app\generated, unless NO_POST=1.
rem Logs: out\codegen_<module>.log, out\post_codegen.log.
call "%~dp0pc_env.bat" || exit /b 1
set "ROOT=%~dp0.."
if not exist "%ROOT%\out" mkdir "%ROOT%\out"
if exist "%ROOT%\out\codegen_ok.txt" del "%ROOT%\out\codegen_ok.txt"
set "REXGLUE=%ROOT%\sdk\out\win-amd64\rexglue.exe"
if not exist "%REXGLUE%" (
  echo == building the host rexglue.exe
  if not exist "%ROOT%\out\host\build.ninja" (
    cmake --fresh -S "%ROOT%\sdk" -B "%ROOT%\out\host" -G Ninja -DCMAKE_BUILD_TYPE=Release %REX_CMAKE_COMPILERS% -DREXGLUE_ENABLE_TRACY=OFF > "%ROOT%\out\host_configure.log" 2>&1 || exit /b 1
  )
  cmake --build "%ROOT%\out\host" --target rexglue -j 14 > "%ROOT%\out\host_build.log" 2>&1 || exit /b 1
)
if not exist "%REXGLUE%" (
  for /r "%ROOT%\out\host" %%F in (rexglue.exe) do if exist "%%F" set "REXGLUE=%%F"
)
if not exist "%REXGLUE%" ( echo rexglue.exe not found & exit /b 1 )
echo == rexglue: %REXGLUE%
set REX_MAX_JUMP_TABLE_ENTRIES=1024
pushd "%ROOT%\app"
for %%Z in (z0070 z0130 z0160 z0161) do (
  echo == codegen default.xex + %%Z.xex
  "%REXGLUE%" codegen renut_manifest.toml --target %%Z --ignore-stamp > "%ROOT%\out\codegen_%%Z.log" 2>&1 || ( popd & exit /b 1 )
)
popd
python "%ROOT%\tools\ffxiii2_post_codegen_modules.py" "%ROOT%\app" || exit /b 1
if "%NO_POST%"=="1" ( echo ok> "%ROOT%\out\codegen_ok.txt" & exit /b 0 )
set "REXSDK_DIR=%ROOT%\sdk"
rem localize_native.py writes app\generated\hot\*.inc and does not create the folder (katamari-notes.md: "a missing generated/hot")
if not exist "%ROOT%\app\generated\hot" mkdir "%ROOT%\app\generated\hot"
python "%ROOT%\tools\post_codegen.py" > "%ROOT%\out\post_codegen.log" 2>&1 || ( echo post_codegen.py failed: out\post_codegen.log & exit /b 1 )
echo == codegen done
rem success marker: the exit code of a .bat run from PowerShell got lost once (job 13:34 printed exit 0 after a failure)
echo ok> "%ROOT%\out\codegen_ok.txt"
