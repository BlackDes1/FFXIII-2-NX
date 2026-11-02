# FFXIII-2 port: one boot test of out\pc\ffxiii2.exe. Always muted and minimized (it never takes over the desktop or
# the speakers), stopped after -Seconds; prints how it ended and the end of its log.
#   powershell -ExecutionPolicy Bypass -File tools\win\boot_test.ps1 [-Seconds 60] [-Tail 60] [-Shots "15,30,60"]
# -Pad: scripted pad input for player 1, 'seconds:BUTTON[:hold],...' (SDK cvar input_script), e.g. "12:A,20:START".
# -Shots: save the game's frame at those seconds to out\pc\captures\shot_<s>s.bmp (works while minimized).
# Do not pipe this into Select-Object -First (that stops the script before it kills the game).
param([int]$Seconds = 60, [int]$Tail = 60, [string]$Shots = '', [string]$Pad = '')
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$exe = Join-Path $repo 'out\pc\ffxiii2.exe'
if (-not (Test-Path $exe)) { throw "$exe not found: run tools\win\build.ps1" }
$dir = Split-Path $exe
Get-Process ffxiii2 -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe } | Stop-Process -Force
$started = Get-Date
$env:REX_FFXIII2_SHOTS = $Shots
$env:REX_INPUT_SCRIPT = $Pad
$env:REX_AUDIO_MUTE = 'true'; $env:REX_WINDOW_START_MINIMIZED = 'true'; $env:REX_FULLSCREEN = 'false'
$p = Start-Process -FilePath $exe -WorkingDirectory $dir -PassThru -WindowStyle Minimized
$p.WaitForExit($Seconds * 1000) | Out-Null
if ($p.HasExited) { 'ffxiii2.exe exited after {0:n1} s, code 0x{1:X8}' -f ((Get-Date) - $started).TotalSeconds, $p.ExitCode }
else {
  $p.Refresh()
  'still running after {0} s: cpu {1:n1} s, {2} MB; stopped' -f $Seconds, $p.TotalProcessorTime.TotalSeconds, [math]::Round($p.WorkingSet64 / 1MB)
  Stop-Process -Id $p.Id -Force; $p.WaitForExit(20000) | Out-Null
}
$log = Get-ChildItem (Join-Path $dir 'logs') -Filter *.log -ErrorAction SilentlyContinue |
  Where-Object { $_.LastWriteTime -ge $started.AddSeconds(-2) } | Sort-Object LastWriteTime | Select-Object -Last 1
if ($log) { "== $($log.FullName)"; Get-Content $log.FullName -Tail $Tail } else { 'no log written' }
