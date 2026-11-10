# ffxiii2-nx: codegen + PC build in one go, for a job runner (paths with spaces handled here, not in cmd).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\ffxiii2_pc_job.ps1 [-NoCodegen] [-NoBuild]
# Logs: out\codegen_*.log, out\post_codegen.log, out\pc_build.log; a summary on stdout.
param([switch]$NoCodegen, [switch]$NoBuild)
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
$works = Split-Path -Parent $root
New-Item -ItemType Directory -Force (Join-Path $root 'out') | Out-Null
$assets = Join-Path $root 'app\assets'
if (-not (Test-Path $assets)) {
  New-Item -ItemType Junction -Path $assets -Target (Join-Path $works 'game_root') | Out-Null
  "app\assets -> $(Join-Path $works 'game_root')"
}
$t0 = Get-Date
if (-not $NoCodegen) {
  & (Join-Path $PSScriptRoot 'ffxiii2_codegen.bat') *> (Join-Path $root 'out\codegen_job.log')
  "codegen exit $LASTEXITCODE ($([int]((Get-Date) - $t0).TotalMinutes) min)"
  Get-Content (Join-Path $root 'out\codegen_job.log') -Tail 8
  if (-not (Test-Path (Join-Path $root 'out\codegen_ok.txt'))) { 'codegen FAILED (no out\codegen_ok.txt)'; exit 1 }
}
if (-not $NoBuild) {
  # ffxiii2-nx (2026-11-05, job k135): ninja does not rebuild the precompiled header when codegen/post_codegen rewrite
  # app\generated\renut_pch.h (the PCH's depfile does not list it), and clang then refuses every object ("has been
  # modified since the precompiled header was built"). A newer renut_pch.h touches cmake_pch.hxx, the PCH's input, so
  # ninja rebuilds it (and everything that uses it). Only a timestamp; nothing deleted.
  $pchH = Join-Path $root 'app\generated\renut_pch.h'
  $pchDir = Join-Path $root 'app\out\pc\CMakeFiles\renut_recomp.dir'
  $pch = Join-Path $pchDir 'cmake_pch.hxx.pch'
  if ((Test-Path $pchH) -and (Test-Path $pch) -and (Get-Item $pchH).LastWriteTime -gt (Get-Item $pch).LastWriteTime) {
    (Get-Item (Join-Path $pchDir 'cmake_pch.hxx')).LastWriteTime = Get-Date
    'renut_pch.h is newer than the PCH: cmake_pch.hxx touched, the PCH will be rebuilt'
  }
  $t1 = Get-Date
  & (Join-Path $PSScriptRoot 'build_pc.bat') *> (Join-Path $root 'out\pc_build.log')
  # The build's own codegen step (app/CMakeLists.txt, renut_codegen + post_codegen.py) declares only its stamp as an
  # output: ninja stats renut_pch.h before that step rewrites it, so the PCH counts as up to date and clang refuses it
  # later in the same build. Then the PCH's input is touched and the build runs once more (the header is final by then).
  if ($LASTEXITCODE -and (Select-String -Path (Join-Path $root 'out\pc_build.log') -SimpleMatch 'has been modified since the precompiled header' -Quiet)) {
    (Get-Item (Join-Path $pchDir 'cmake_pch.hxx')).LastWriteTime = Get-Date
    "PC build exit $LASTEXITCODE on a stale PCH ($([int]((Get-Date) - $t1).TotalMinutes) min): cmake_pch.hxx touched, building again"
    Copy-Item -Force (Join-Path $root 'out\pc_build.log') (Join-Path $root 'out\pc_build_stale_pch.log')
    & (Join-Path $PSScriptRoot 'build_pc.bat') *> (Join-Path $root 'out\pc_build.log')
  }
  "PC build exit $LASTEXITCODE ($([int]((Get-Date) - $t1).TotalMinutes) min)"
  Get-Content (Join-Path $root 'out\pc_build.log') -Tail 15
  exit $LASTEXITCODE
}
