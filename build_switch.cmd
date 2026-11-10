@echo off
rem Final Fantasy XIII-2 for the Switch: builds the SD card folder from your own game files (tools\gh\build_switch.ps1).
rem   build_switch.cmd -Iso "D:\Games\FFXIII-2.iso" -Update "D:\Games\tu0000000b_00000000" [-Dlc "D:\Games\DLC"]
rem   or double-click it: it asks for the paths.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\gh\build_switch.ps1" %*
set "RC=%ERRORLEVEL%"
if "%~1"=="" pause
exit /b %RC%
