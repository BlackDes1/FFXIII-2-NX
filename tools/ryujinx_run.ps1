# ffxiii2-nx (2026-11-04, the user: Ryujinx instead of Eden for every emulator run): one test run of the Switch NRO in
# Ryujinx (Port\ryujinx\Ryujinx.exe, 1.3.3, user-profile mode: config, keys and SD card in %APPDATA%\Ryujinx).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\ryujinx_run.ps1 -Name r1 [-Seconds 300] [-Toml <file>]
#     [-Nro <file>]
#
# - Ryujinx starts an NRO without a loader (no argv, no process handle), so the SDK takes the NRO's usual place on the SD
#   (REX_SWITCH_FALLBACK_EXE = /switch/ffxiii2-nx/ffxiii2.nro, app/CMakeLists.txt) and maps guest views eagerly by
#   itself (no data aborts there; sdk/src/core/guest_memory_switch.cpp). This port's own SD folder is
#   %APPDATA%\Ryujinx\sdcard\switch\ffxiii2-nx: the NRO, the player toml (ffxiii2.toml) and two junctions, game_root
#   (Works\game_root: disc + TU6 .xexp) and update (Works\game_data\tu). Nothing else in the user's Ryujinx setup is
#   touched.
# - One run at a time: it does not start while any Ryujinx process runs (other ports run theirs), nor with less than
#   8 GB of free commit memory. It stops only the process it started (by id).
# - Collected into out\ryujinx_runs\<Name>: Ryujinx's log of this start (ryujinx\Logs, the newest after the start), the
#   SDK's logs from the SD folder (logs\, rex_crash.log, deko3d.log) and the captures (capturas\: nfsmw's name).
param([Parameter(Mandatory)][string]$Name, [int]$Seconds = 300, [string]$Toml = '', [string]$Nro = '')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$works = Split-Path -Parent $repo
$port = Split-Path -Parent $works
$ryu = Join-Path $port 'ryujinx\Ryujinx.exe'
$ryuLogs = Join-Path $port 'ryujinx\Logs'
$sd = Join-Path $env:APPDATA 'Ryujinx\sdcard\switch\ffxiii2-nx'
$out = Join-Path $repo "out\ryujinx_runs\$Name"
if (-not (Test-Path $ryu)) { throw "no Ryujinx at $ryu" }
New-Item -ItemType Directory -Force $out, $sd | Out-Null

# This port's SD folder.
foreach ($link in @(@('game_root', (Join-Path $works 'game_root')), @('update', (Join-Path $works 'game_data\tu')))) {
  $p = Join-Path $sd $link[0]
  if (-not (Test-Path $p)) { New-Item -ItemType Junction -Path $p -Target $link[1] | Out-Null; "junction $p -> $($link[1])" }
}
if (-not $Nro) { $Nro = Join-Path $repo 'app\out\switch\renut.nro' }
Copy-Item -Force $Nro (Join-Path $sd 'ffxiii2.nro')
if ($Toml) { Copy-Item -Force $Toml (Join-Path $sd 'ffxiii2.toml') }
# Old SD logs and captures out of the way (renamed, not deleted), so this run's are the only ones collected.
foreach ($d in 'logs', 'captures', 'capturas') {
  $p = Join-Path $sd $d
  if (Test-Path $p) { Rename-Item $p ("{0}_before_{1}" -f $d, $Name) -ErrorAction SilentlyContinue }
}

# One run at a time, and enough memory.
$running = Get-Process Ryujinx -ErrorAction SilentlyContinue
if ($running) { "a Ryujinx is running (ids $($running.Id -join ', ')): not started"; exit 2 }
$os = Get-CimInstance Win32_OperatingSystem
$freeGb = [math]::Round($os.FreeVirtualMemory / 1MB, 1)
if ($freeGb -lt 8) { "free commit $freeGb GB (< 8): not started"; exit 3 }

$t0 = Get-Date
$nroPath = Join-Path $sd 'ffxiii2.nro'
"start $t0  ryujinx $ryu  nro $nroPath  seconds $Seconds  free commit $freeGb GB" | Set-Content (Join-Path $out 'run.txt')
$p = Start-Process -FilePath $ryu -ArgumentList ('"' + $nroPath + '"') -WorkingDirectory (Split-Path $ryu) -PassThru
"started pid $($p.Id)"
while (((Get-Date) - $t0).TotalSeconds -lt $Seconds -and -not $p.HasExited) { Start-Sleep -Seconds 2 }
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; "stopped pid $($p.Id) after $Seconds s" }
else { "exited by itself with code $($p.ExitCode) after $([int]((Get-Date) - $t0).TotalSeconds) s" }
Start-Sleep -Seconds 2
"end $(Get-Date)" | Add-Content (Join-Path $out 'run.txt')

# Collect.
Get-ChildItem $ryuLogs -Filter '*.log' | Where-Object { $_.LastWriteTime -gt $t0 } | Copy-Item -Destination $out -Force
foreach ($d in 'logs', 'captures', 'capturas') {
  $p = Join-Path $sd $d
  if (Test-Path $p) { Copy-Item -Recurse -Force $p (Join-Path $out $d) }
}
foreach ($f in 'deko3d.log', 'rex_crash.log', 'rex_stderr.log') {
  $p = Join-Path $sd $f
  if (Test-Path $p) { Copy-Item -Force $p $out }
}
# Without argv[0] (Ryujinx) the SDK's own logs (rex_profile.log of switch_profile_stacks, rex_crash.log,
# rex_stderr.log) go to sdmc:/switch/ (sdk/src/ui/switch_crash_hooks.c): the ones written during this run.
$sdSwitch = Split-Path -Parent $sd
foreach ($f in 'rex_profile.log', 'rex_crash.log', 'rex_stderr.log') {
  $p = Join-Path $sdSwitch $f
  if ((Test-Path $p) -and (Get-Item $p).LastWriteTime -gt $t0) { Copy-Item -Force $p (Join-Path $out ('sd_' + $f)) }
}
Get-ChildItem -Recurse $out | Select-Object FullName, Length | Format-Table -AutoSize
