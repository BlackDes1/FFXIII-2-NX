# FFXIII-2 port: lays out the Switch SD card folder out\nx\sdcard\switch\ffxiii2-nx (the NRO and its settings).
# The game files are yours and stay out of the repository:
#   -LinkGame   game_root\ and update\ as directory junctions to Works\game_root and Works\game_data\tu (for an
#               emulator whose SD card is this sdcard folder: nothing is copied);
#   -CopyGame   copies both folders (for a real SD card; ~8 GB).
# SD card layout:
#   sdmc:/switch/ffxiii2-nx/ffxiii2.nro
#   sdmc:/switch/ffxiii2-nx/ffxiii2.toml      your options (app\ffxiii2_switch.toml); everything else is built in
#   sdmc:/switch/ffxiii2-nx/game_root/        default.xex + default.xexp, z*.xex + z*.xexp, alba_data\, nxeart
#   sdmc:/switch/ffxiii2-nx/update/           the TU6 data files (the PC's update_data_root)
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\package_switch.ps1 [-LinkGame | -CopyGame]
param([switch]$LinkGame, [switch]$CopyGame)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$works = Split-Path $repo -Parent
$dest = Join-Path $repo 'out\nx\sdcard\switch\ffxiii2-nx'
New-Item -ItemType Directory -Force $dest | Out-Null
$nro = Join-Path $repo 'out\nx\ffxiii2.nro'
if (-not (Test-Path $nro)) { throw "no NRO at ${nro}: run tools\win\build_switch.ps1 first" }
Copy-Item -Force $nro (Join-Path $dest 'ffxiii2.nro')
if (-not (Test-Path (Join-Path $dest 'ffxiii2.toml'))) {
  Copy-Item (Join-Path $repo 'app\ffxiii2_switch.toml') (Join-Path $dest 'ffxiii2.toml')
}
$pairs = @(@('game_root', (Join-Path $works 'game_root')), @('update', (Join-Path $works 'game_data\tu')))
foreach ($p in $pairs) {
  $link = Join-Path $dest $p[0]
  if ($LinkGame -and -not (Test-Path $link)) {
    & $env:ComSpec /c "mklink /J `"$link`" `"$($p[1])`"" | Out-Null
    if ($LASTEXITCODE) { throw "mklink /J failed for $link" }
  } elseif ($CopyGame) {
    robocopy $p[1] $link /E /NFL /NDL /NJH /NJS | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed for $link" }
  }
}
Get-ChildItem $dest | Select-Object Name, Length, Attributes
