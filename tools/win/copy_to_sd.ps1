# FFXIII-2 port: copies the Switch test package and the game files to a Switch SD card.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\copy_to_sd.ps1 -Sd H:\ [-Package out\nx\console_pkg]
# Result on the card:
#   <sd>\switch\ffxiii2-nx\ffxiii2.nro, ffxiii2.toml      (the package; an existing ffxiii2.toml is kept)
#   <sd>\switch\ffxiii2-nx\game_root\                     (Works\game_root: disc files + TU6 .xexp; alba_data followed)
#   <sd>\switch\ffxiii2-nx\update\                        (Works\game_data\tu)
# Game files already on the card with the same size and time are skipped (robocopy), so a new build only copies the NRO.
# Every copied file is then compared by size with its source; the summary is printed at the end.
param([Parameter(Mandatory)][string]$Sd, [string]$Package = '')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$works = Split-Path $repo -Parent
if (-not $Package) { $Package = Join-Path $repo 'out\nx\console_pkg' }
$src = Join-Path $Package 'switch\ffxiii2-nx'
if (-not (Test-Path (Join-Path $src 'ffxiii2.nro'))) { throw "no package at $src" }
if (-not (Test-Path (Join-Path $Sd 'atmosphere'))) { throw "$Sd does not look like an Atmosphere SD card (no atmosphere folder)" }
$dest = Join-Path $Sd 'switch\ffxiii2-nx'

$vol = Get-Volume -DriveLetter $Sd.Substring(0, 1)
# Sizes from Works\game_data (disc + tu): game_root is links to those files (hard links, and a junction for alba_data,
# which Get-ChildItem may not follow).
$disc = Join-Path $works 'game_data\disc'; $tu = Join-Path $works 'game_data\tu'
$need = (Get-ChildItem -Recurse -File $disc, $tu, $tu, $src | Measure-Object Length -Sum).Sum
$have = if (Test-Path $dest) { (Get-ChildItem -Recurse -File $dest -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum } else { 0 }
"SD {0}: {1}, {2:N1} GB free of {3:N1} GB; to copy {4:N1} GB (already there {5:N1} GB)" -f $Sd, $vol.FileSystemType,
  ($vol.SizeRemaining / 1GB), ($vol.Size / 1GB), ($need / 1GB), ($have / 1GB)
if ($vol.SizeRemaining + $have -lt $need + 256MB) { throw 'not enough free space on the SD card' }
if ($vol.FileSystemType -eq 'FAT32') {
  $big = Get-ChildItem -Recurse -File $disc | Where-Object Length -ge 4GB
  if ($big) { throw "FAT32 cannot hold files of 4 GB or more: $($big.FullName -join ', ')" }
}

$t0 = Get-Date
New-Item -ItemType Directory -Force $dest | Out-Null
Copy-Item -Force (Join-Path $src 'ffxiii2.nro') $dest
if (-not (Test-Path (Join-Path $dest 'ffxiii2.toml'))) { Copy-Item (Join-Path $src 'ffxiii2.toml') $dest }
foreach ($p in @(@((Join-Path $works 'game_root'), 'game_root'), @((Join-Path $works 'game_data\tu'), 'update'))) {
  # /E subfolders, /R:2 /W:2 short retries; junctions (alba_data) are followed by default.
  robocopy $p[0] (Join-Path $dest $p[1]) /E /R:2 /W:2 /NP /NFL /NDL /NJH | Out-Host
  if ($LASTEXITCODE -ge 8) { throw "robocopy failed for $($p[1]) (exit $LASTEXITCODE)" }
}
"copy took {0:N1} min" -f ((Get-Date) - $t0).TotalMinutes

# Check: every disc file is in game_root, every TU file in update (and its .xexp in game_root), the NRO in place, all
# with the same size.
$bad = 0; $n = 0
foreach ($f in Get-ChildItem $tu -Filter *.xexp) {
  $d = Join-Path $dest "game_root\$($f.Name)"; $n++
  if (-not (Test-Path $d) -or (Get-Item $d).Length -ne $f.Length) { $bad++; "MISMATCH game_root\$($f.Name)" }
}
foreach ($p in @(@($disc, 'game_root'), @($tu, 'update'), @($src, ''))) {
  $root = (Resolve-Path $p[0]).Path
  foreach ($f in Get-ChildItem -Recurse -File $root) {
    $rel = $f.FullName.Substring($root.Length).TrimStart('\')
    if ($p[1] -eq '' -and $rel -eq 'ffxiii2.toml') { continue }
    $d = if ($p[1]) { Join-Path (Join-Path $dest $p[1]) $rel } else { Join-Path $dest $rel }
    $n++
    if (-not (Test-Path $d) -or (Get-Item $d).Length -ne $f.Length) { $bad++; "MISMATCH $rel" }
  }
}
"checked $n files: $bad mismatches"
Get-ChildItem $dest | Format-Table Name, Length, LastWriteTime -AutoSize | Out-String
if ($bad) { exit 1 }
