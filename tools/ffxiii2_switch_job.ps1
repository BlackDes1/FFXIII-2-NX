# ffxiii2-nx: the Switch build (tools/build_switch.bat's steps) for a job runner on this PC: the VS Build Tools' CMake
# and Ninja from sa2-nx's env.ps1 (MSYS2 off PATH: its cmake took the Windows paths for MSYS ones), DEVKITPRO as a
# Windows path, deko3d only (REXGLUE_SWITCH_NVK stays OFF: no NVK linked). Log: out\switch_build.log.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\ffxiii2_switch_job.ps1 [-Jobs N] [-Reconfigure]
#       [-BuildDir app\out\switch-pgo-gen] [-Extra '-DBANJO_PGO=generate'] [-Log out\x.log]
# ffxiii2-nx (2026-11-07, PGO/LTO allowed by the user): -BuildDir / -Extra / -Log for the PGO and LTO build folders
# (app/CMakeLists.txt, BANJO_PGO and BANJO_SWITCH_LTO, from sa2-nx / banjo-nx docs/pgo-lto.md). -Extra is split at
# spaces and only used when the folder is configured. Defaults: the everyday folder, as before.
param([int]$Jobs = 0, [switch]$Reconfigure, [string]$BuildDir = 'app\out\switch', [string]$Extra = '',
      [string]$Log = 'out\switch_build.log')
$ErrorActionPreference = 'Stop'
$ffRoot = Split-Path -Parent $PSScriptRoot
$ffWorks = Split-Path -Parent $ffRoot
$sa2Env = Join-Path $ffWorks 'sa2-nx\tools\win\env.ps1'
# ffxiii2-nx (2026-11-08, GitHub package): without the original PC's sa2-nx tree, tools\gh\env.ps1 (vswhere) instead.
if (Test-Path $sa2Env) { . $sa2Env } else { . (Join-Path $PSScriptRoot 'gh\env.ps1') }  # sa2's sets its own $root / $works, so ours have other names
$ErrorActionPreference = 'Continue'
if ($Jobs -le 0) {
  $ramGb = [math]::Floor((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB)
  $Jobs = [math]::Max(1, [math]::Min([Environment]::ProcessorCount, [math]::Floor($ramGb / 2)))
}
if (-not $env:DEVKITPRO) { $env:DEVKITPRO = 'C:/devkitPro' }
$env:DEVKITPRO = $env:DEVKITPRO.Replace('\', '/')
$py = if ($env:FFX_PYTHON) { $env:FFX_PYTHON.Replace('\', '/') } else { 'C:/Python314/python.exe' }
$nvk = (Join-Path $ffWorks '_tools\mesa-switch\opt\devkitpro\portlibs\switch').Replace('\', '/')
# GitHub package: NVK is not linked (REXGLUE_SWITCH_NVK OFF); without the Mesa SDK the folder is devkitPro's portlibs.
if ($env:FFX_NVK_SDK) { $nvk = $env:FFX_NVK_SDK.Replace('\', '/') } elseif (-not (Test-Path $nvk)) { $nvk = "$($env:DEVKITPRO)/portlibs/switch" }
$build = Join-Path $ffRoot $BuildDir
$log = Join-Path $ffRoot $Log
$extraArgs = @($Extra -split ' ' | Where-Object { $_ })
$tc = (Join-Path $ffRoot 'tools\switch\cmake\switch-devkitA64.cmake').Replace('\', '/')
"== $(Get-Date) jobs $Jobs" | Set-Content $log
if ($Reconfigure -or -not (Test-Path (Join-Path $build 'CMakeCache.txt'))) {
  & cmake -S (Join-Path $ffRoot 'app') -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DKATAMARI_RENUT_HOOKS=ON "-DCMAKE_TOOLCHAIN_FILE=$tc" `
      "-DREXSDK_DIR=$((Join-Path $ffRoot 'sdk').Replace('\', '/'))" "-DREXGLUE_SWITCH_NVK_SDK=$nvk" `
      "-DPYTHON_EXECUTABLE=$py" "-DPython3_EXECUTABLE=$py" @extraArgs *>> $log
  if ($LASTEXITCODE) { "configure failed ($LASTEXITCODE)"; Get-Content $log -Tail 40; exit 1 }
}
# ffxiii2-nx (2026-11-05): a renut_pch.h newer than the precompiled header touches cmake_pch.hxx, so ninja rebuilds the
# PCH (its depfile does not list renut_pch.h; tools/ffxiii2_pc_job.ps1 has the same guard). Only a timestamp.
$pchH = Join-Path $ffRoot 'app\generated\renut_pch.h'
$pchDir = Join-Path $build 'CMakeFiles\renut_recomp.dir'
$pch = Get-ChildItem $pchDir -Filter 'cmake_pch.hxx.*ch' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($pch -and (Test-Path $pchH) -and (Get-Item $pchH).LastWriteTime -gt $pch.LastWriteTime) {
  (Get-Item (Join-Path $pchDir 'cmake_pch.hxx')).LastWriteTime = Get-Date
  "renut_pch.h is newer than $($pch.Name): cmake_pch.hxx touched, the PCH will be rebuilt" | Tee-Object -Append $log
}
& cmake --build $build -j $Jobs *>> $log
$code = $LASTEXITCODE
"switch build exit $code"
Get-Content $log -Tail 25
exit $code
