@echo off
rem katamari-nx: the released NRO, in a build folder of its own (docs/building.md): LTO, the PGO profile in pgo/, no game
rem shader inside (it reads deko3d_glsl.bin beside itself). tools\renut_release_shaders.py makes the files it builds with.
rem Output: app\out\switch-pgo-use\renut.nro
set "BUILD_DIR=%~dp0..\app\out\switch-pgo-use"
set "CMAKE_EXTRA=-DBANJO_PGO=use -DBANJO_SWITCH_LTO=ON -DBANJO_RENUT_RELEASE_SHADERS=ON"
if "%JOBS%"=="" set "JOBS=10"
call "%~dp0build_switch.bat" %*
