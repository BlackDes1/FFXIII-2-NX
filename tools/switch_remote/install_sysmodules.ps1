# ffxiii2-nx (2026-11-05): installs sys-botbase (masagrator 2.6-kill) and sys-ftpd-light (Thorhax 1.0.5-thorhax1)
# from their release ZIPs onto the Switch SD card in the PC's card reader, without touching anything else.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\switch_remote\install_sysmodules.ps1 -Sd H: `
#       -BotbaseZip ..\_xfer\sys-botBase.zip -FtpdZip ..\_xfer\sys-ftpd-light-1.0.5-thorhax1.zip -FtpdIni <config.ini>
#
# Checks: each ZIP's SHA-256 against the release page's; the ZIPs contain only atmosphere\contents\<id>\ (and the
# ftpd example config); nothing that already exists on the card is overwritten: an existing sysmodule folder or
# config.ini is first copied to <SD>\switch\ffxiii2-nx\before_sysmodules\ and then left alone unless -Replace.
# Atmosphere's own configuration (atmosphere\config, system_settings.ini, hosts) is not touched.
param(
  [Parameter(Mandatory = $true)][string]$Sd,
  [Parameter(Mandatory = $true)][string]$BotbaseZip,
  [Parameter(Mandatory = $true)][string]$FtpdZip,
  [Parameter(Mandatory = $true)][string]$FtpdIni,
  [switch]$Replace
)
$ErrorActionPreference = 'Stop'
$want = @{
  'sys-botBase.zip' = '9A2273C4A4FE09D80F1F8B981E274424D47658992EC770E238D8551DC250733D'
  'sys-ftpd-light-1.0.5-thorhax1.zip' = '9C1A3747AD0A32641305B458481CC5626155FC02ADB2243CC32E6EFD2035BC37'
}
$sdRoot = $Sd.TrimEnd('\') + '\'
if (-not (Test-Path (Join-Path $sdRoot 'atmosphere\contents'))) { throw "no atmosphere\contents on $sdRoot (wrong drive?)" }
$stage = Join-Path $env:TEMP ('ffxiii2_sysmods_' + (Get-Date -Format 'yyyyMMdd_HHmmss'))
New-Item -ItemType Directory -Force $stage | Out-Null
foreach ($zip in @($BotbaseZip, $FtpdZip)) {
  $name = Split-Path $zip -Leaf
  $h = (Get-FileHash -Algorithm SHA256 $zip).Hash
  if ($h -ne $want[$name]) { throw "$name SHA-256 $h is not the release's $($want[$name])" }
  "$name SHA-256 ok ($h)"
  Expand-Archive -Path $zip -DestinationPath (Join-Path $stage $name) -Force
}
$backup = Join-Path $sdRoot 'switch\ffxiii2-nx\before_sysmodules'
$copied = 0
foreach ($dir in Get-ChildItem $stage -Directory) {
  foreach ($f in Get-ChildItem $dir.FullName -Recurse -File) {
    $rel = $f.FullName.Substring($dir.FullName.Length + 1)
    if (-not ($rel -like 'atmosphere\contents\4*' -or $rel -like 'config\sys-ftpd-10k\*')) { throw "unexpected file in the ZIP: $rel" }
    if ($rel -like 'config\sys-ftpd-10k\config.ini') { continue }   # written from -FtpdIni below
    $dst = Join-Path $sdRoot $rel
    if (Test-Path $dst) {
      $b = Join-Path $backup $rel
      New-Item -ItemType Directory -Force (Split-Path $b) | Out-Null
      Copy-Item $dst $b -Force
      if (-not $Replace) { "exists, kept (backup in before_sysmodules): $rel"; continue }
    }
    New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
    Copy-Item $f.FullName $dst
    $copied++
    "installed $rel ($($f.Length) bytes)"
  }
}
$ini = Join-Path $sdRoot 'config\sys-ftpd-10k\config.ini'
if (Test-Path $ini) {
  $b = Join-Path $backup 'config\sys-ftpd-10k\config.ini'
  New-Item -ItemType Directory -Force (Split-Path $b) | Out-Null
  Copy-Item $ini $b -Force
  if (-not $Replace) { 'config.ini exists, kept (backup in before_sysmodules)' } else { Copy-Item $FtpdIni $ini -Force; 'config.ini replaced' }
} else {
  New-Item -ItemType Directory -Force (Split-Path $ini) | Out-Null
  Copy-Item $FtpdIni $ini
  'config.ini written (user set, anonymous:=0, port 5000)'
}
$text = Get-Content $ini -Raw
if ($text -notmatch 'anonymous:=0') { throw 'config.ini does not disable anonymous access' }
if ($text -match '(?m)^user:=\s*$' -or $text -match '(?m)^password:=\s*$') { throw 'config.ini has an empty user or password' }
foreach ($id in @('430000000000000B', '420000000000000E')) {
  $d = Join-Path $sdRoot "atmosphere\contents\$id"
  $ok = (Test-Path (Join-Path $d 'exefs.nsp')) -and (Test-Path (Join-Path $d 'flags\boot2.flag'))
  "$id exefs.nsp + boot2.flag: $ok"
}
Remove-Item -Recurse -Force $stage
"$copied files installed; reboot the Switch into CFW (docked Joy-Con HOME LED lights up when sys-botbase runs)"
