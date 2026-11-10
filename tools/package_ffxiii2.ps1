# ffxiii2-nx (2026-11-05, work order 5.7): the console package, as katamari-nx's tools/package_switch.ps1:
#   Works\builds\<date>_<time>_<name>\
#     COPY_TO_SD_switch_ffxiii2-nx\   ffxiii2.nro, ffxiii2.toml (= renut.toml), configs\ (renut.toml, renut_play.toml,
#                                     renut_ab.toml, renut_profile.toml), deko3d_shaders.bin (the DKSH cache Ryujinx's
#                                     runs compiled with the same uam, if present), game_root\ and update\ (empty:
#                                     your own disc files and the TU6 data; nothing of the game goes in here)
#     ffxiii2-nx.elf                  the unstripped ELF of this NRO (profiles map to it)
#     source_<commit>.tgz             the source snapshot (git archive of the commit; no game data, no generated code)
#     README.txt                      dist\switch\ffxiii2-nx\README.txt (install, test, A/B and profiling instructions)
#     PACKAGE_NOTES.txt               -Notes, if given
#   powershell -File tools\package_ffxiii2.ps1 -Name rc1 [-Cache <deko3d_shaders.bin>] -Source <source_<commit>.tgz> [-Build app\out\switch] [-Notes <file>]
# The PC tree has no git: the source snapshot is the cloud tree's `git archive` of the commit the build was made from.
# Copies only; it deletes nothing. It stops when C: has less than 5 GB free.
param([string]$Name = "package", [string]$Build = "app\out\switch", [string]$Notes = "", [string]$Source = "", [string]$Cache = "")
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$free = (Get-PSDrive C).Free / 1GB
if ($free -lt 5) { throw ("C: has {0:N1} GB free: not packaging (stop and say so)" -f $free) }
$b = Join-Path $root $Build
$nro = Join-Path $b "renut.nro"
$elf = Join-Path $b "renut"
if (-not (Test-Path $nro)) { throw "no NRO at $nro" }
$stamp = Get-Date -Format "yyyy-MM-dd_HHmm"
$out = Join-Path (Split-Path -Parent $root) "builds\${stamp}_ffxiii2_$Name"
$sd = Join-Path $out "COPY_TO_SD_switch_ffxiii2-nx"
New-Item -ItemType Directory -Force $sd, (Join-Path $sd "configs"), (Join-Path $sd "game_root"), (Join-Path $sd "update") | Out-Null
Copy-Item $nro (Join-Path $sd "ffxiii2.nro")
$dist = Join-Path $root "dist\switch\ffxiii2-nx"
Copy-Item (Join-Path $dist "renut.toml") (Join-Path $sd "ffxiii2.toml")
Copy-Item (Join-Path $dist "*.toml") (Join-Path $sd "configs")
# -Cache: a DKSH cache compiled on the PC (tools/deko3d_variant_cache.py), else the one Ryujinx's runs wrote.
$cache = if ($Cache) { $Cache } else { Join-Path $env:APPDATA "Ryujinx\sdcard\switch\ffxiii2-nx\deko3d_shaders.bin" }
if (Test-Path $cache) { Copy-Item $cache (Join-Path $sd "deko3d_shaders.bin"); "deko3d_shaders.bin from $($cache): $((Get-Item $cache).Length) bytes" }
Set-Content -Path (Join-Path $sd "game_root\PUT_YOUR_GAME_FILES_HERE.txt") -Encoding ascii -Value `
    "Extract your own Final Fantasy XIII-2 (EU) discs here: default.xex, the data and the rest (README.txt)."
Set-Content -Path (Join-Path $sd "update\PUT_THE_TITLE_UPDATE_HERE.txt") -Encoding ascii -Value `
    "The title update 6 files (default.xexp and its data) here (README.txt)."
if (Test-Path $elf) { Copy-Item $elf (Join-Path $out "ffxiii2-nx.elf") }
Copy-Item (Join-Path $dist "README.txt") $out
if ($Notes) { Copy-Item $Notes (Join-Path $out "PACKAGE_NOTES.txt") }
if ($Source) { Copy-Item $Source $out; $sha = [IO.Path]::GetFileNameWithoutExtension($Source) -replace '^source_', '' } else { $sha = "no snapshot"; "no -Source: no source snapshot in the package" }
$md5 = (Get-FileHash -Algorithm MD5 (Join-Path $sd "ffxiii2.nro")).Hash.ToLower()
Write-Output "package: $out"
Write-Output ("NRO {0:N1} MB, md5 {1}; commit {2}" -f ((Get-Item (Join-Path $sd "ffxiii2.nro")).Length / 1MB), $md5, $sha)
Get-ChildItem -Recurse $out | Where-Object { -not $_.PSIsContainer } | Select-Object FullName, Length | Format-Table -AutoSize
