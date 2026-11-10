@echo off
rem Configure + build the Windows (PC) app against the port SDK (katamari-nx\sdk).
rem Usage: tools\build_pc.bat [extra cmake --build args]
rem   BUILD_DIR (env) defaults to app\out\pc; REXSDK_DIR defaults to katamari-nx\sdk.
rem   RENUT (env) = ON builds Renut's renderer in (BANJO_RENUT_RENDERER); default OFF until its hooks are at
rem   Beautiful Katamari's addresses (docs/katamari-notes.md). HOOKS (env) = 1 turns those hooks on (KATAMARI_RENUT_HOOKS).
call "%~dp0pc_env.bat" || exit /b 1
set "ROOT=%~dp0.."
if "%BUILD_DIR%"=="" set "BUILD_DIR=%ROOT%\app\out\pc"
if "%REXSDK_DIR%"=="" set "REXSDK_DIR=%ROOT%\sdk"
rem ffxiii2-nx: RENUT defaults to ON (an OFF build does not link: display_sync). HOOKS defaults to ON since 2026-11-05
rem (the hook files are at FF XIII-2's addresses; a job without $env:HOOKS built with them off and failed to compile).
if "%RENUT%"=="" set "RENUT=ON"
if "%HOOKS%"=="" set "HOOKS=ON"
rem ffxiii2-nx: TRACE=ON builds the Direct3D call trace (src/ffxiii2/ffxiii2_d3d_trace.cpp).
if "%TRACE%"=="" set "TRACE=OFF"
if not exist "%BUILD_DIR%\build.ninja" (
  cmake --fresh -S "%ROOT%\app" -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release %REX_CMAKE_COMPILERS% -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_USE_D3D12=ON -DREXGLUE_USE_VULKAN=ON "-DREXSDK_DIR=%REXSDK_DIR%" -DBANJO_RENUT_RENDERER=%RENUT% -DKATAMARI_RENUT_HOOKS=%HOOKS% -DFFXIII2_D3D_TRACE=%TRACE% || exit /b 1
) else (
  cmake -S "%ROOT%\app" -B "%BUILD_DIR%" -DBANJO_RENUT_RENDERER=%RENUT% -DKATAMARI_RENUT_HOOKS=%HOOKS% -DFFXIII2_D3D_TRACE=%TRACE% || exit /b 1
)
cmake --build "%BUILD_DIR%" -j 12 %*
