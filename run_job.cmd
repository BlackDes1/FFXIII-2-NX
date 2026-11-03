@echo off
rem Runs out\job.txt (tools\win\run_job.ps1) in a minimized window.
if not "%~1"=="min" (start "ffxiii2 job" /min cmd /c "%~f0" min & exit /b)
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\run_job.ps1
