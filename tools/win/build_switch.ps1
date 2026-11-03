# FFXIII-2 port: builds the Nintendo Switch NRO, out\nx\ffxiii2.nro (devkitA64 GCC + libnx, static NVK from the Mesa
# Horizon SDK). The generated code must already exist (tools\win\codegen.ps1 with the PC SDK): a cross build cannot
# run rexglue. Method and toolchain: sa2-nx tools\win\build_switch.ps1 (no LTO and no PGO yet: first light).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\build_switch.ps1 [-Jobs N] [-Reconfigure]
#
# Needs: devkitPro with devkitA64 + libnx (C:\devkitPro, or $env:DEVKITPRO as a Windows path), the Mesa Horizon SDK
# unpacked to Works\_tools\mesa-switch (mesa-26.2.4-switch-unified-horizon-sdk.zip; override with $env:NVK_SDK = its
# opt\devkitpro\portlibs\switch folder), and the VS 2022 Build Tools' CMake + Ninja (found by sa2-nx's env.ps1).
# Logs: out\nx_configure.log, out\nx_build.log.
param([int]$Jobs = 0, [switch]$Reconfigure)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$works = Split-Path $repo -Parent
. (Join-Path $works 'sa2-nx\tools\win\env.ps1')   # CMake and Ninja on PATH, MSYS2 off it (the compilers come from the toolchain file)
$ErrorActionPreference = 'Stop'
if ($Jobs -le 0) {
  # One job per core, but at most one per 2 GB of RAM: GCC -O3 on a 2.5 MB generated file takes well over 1 GB.
  $ramGb = [math]::Floor((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB)
  $Jobs = [math]::Max(1, [math]::Min([Environment]::ProcessorCount, [math]::Floor($ramGb / 2)))
  Write-Host "== $([Environment]::ProcessorCount) cores, $ramGb GB RAM"
}
$nvk = if ($env:NVK_SDK) { $env:NVK_SDK } else { Join-Path $works '_tools\mesa-switch\opt\devkitpro\portlibs\switch' }
if (-not (Test-Path (Join-Path $nvk 'lib\libvulkan.a'))) { throw "no lib\libvulkan.a under $nvk (unpack the Mesa Horizon SDK zip there)" }
# devkitPro's installer exports DEVKITPRO as an MSYS path (/opt/devkitpro) that Windows CMake cannot open.
$devkitpro = if ($env:DEVKITPRO -and (Test-Path $env:DEVKITPRO)) { $env:DEVKITPRO } else { 'C:/devkitPro' }
if (-not (Test-Path (Join-Path $devkitpro 'devkitA64\bin\aarch64-none-elf-g++.exe'))) { throw "devkitA64 not found under $devkitpro" }
# The toolchain file prefers $env:DEVKITPRO over -DDEVKITPRO, and CMake's compiler checks (try_compile) see only the
# environment: the installer's /opt/devkitpro made the first configure fail ("libnx linker specs not found").
$env:DEVKITPRO = $devkitpro.Replace('\', '/')
# From here on native tools write warnings to stderr (CMake deprecation notices): with 'Stop', Windows PowerShell 5.1
# turns the first such line into a terminating error. Exit codes are checked instead.
$ErrorActionPreference = 'Continue'
# The Mesa 26.2.4 Horizon SDK's libvulkan.a carries the Rust core library (NAK and nil are Rust), but its archive
# index lacks those members: the first link failed with undefined core::panicking::* references from nil, although
# the archive defines them (nm: 13,399 index entries before, 18,601 after re-indexing; panic_fmt only after). Re-index
# it once with devkitA64's ranlib; the original is kept as libvulkan.a.orig.
$vk = Join-Path $nvk 'lib\libvulkan.a'
if (-not (Test-Path "$vk.orig")) {
  Copy-Item $vk "$vk.orig"
  & (Join-Path $devkitpro 'devkitA64\bin\aarch64-none-elf-gcc-ranlib.exe') $vk
  if ($LASTEXITCODE) { throw "ranlib failed on $vk" }
}
$build = Join-Path $repo 'out\nx'
$log = Join-Path $repo 'out'
$tc = (Join-Path $repo 'tools\switch\cmake\switch-devkitA64.cmake').Replace('\', '/')
if ($Reconfigure -or -not (Test-Path (Join-Path $build 'CMakeCache.txt'))) {
  Write-Host '== configuring out\nx'
  & cmake --fresh -S (Join-Path $repo 'app') -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release `
      "-DCMAKE_TOOLCHAIN_FILE=$tc" "-DREXGLUE_SWITCH_NVK_SDK=$($nvk.Replace('\', '/'))" "-DDEVKITPRO=$($devkitpro.Replace('\', '/'))" `
      "-DCMAKE_MAKE_PROGRAM=$((Get-Command ninja).Source)" *> (Join-Path $log 'nx_configure.log')
  if ($LASTEXITCODE) { Get-Content (Join-Path $log 'nx_configure.log') -Tail 40; throw 'configure failed: out\nx_configure.log' }
}
Write-Host "== building with $Jobs jobs (the first build compiles ~200 MB of generated C++ with GCC: allow an hour or more)"
$t0 = Get-Date
& cmake --build $build -j $Jobs *> (Join-Path $log 'nx_build.log')
$rc = $LASTEXITCODE
Write-Host ("== build took {0:N0} min" -f ((Get-Date) - $t0).TotalMinutes)
if ($rc) { Get-Content (Join-Path $log 'nx_build.log') -Tail 60; throw 'build failed: out\nx_build.log' }
Get-ChildItem $build -Filter 'ffxiii2*' | Where-Object { $_.Extension -in '.nro', '' -or $_.Name -eq 'ffxiii2.elf' } | Format-Table Name, Length -AutoSize
# Keep the ELF of every NRO that is tested: crash logs and profiler samples are image+0x... offsets for addr2line.
$elfDir = Join-Path $build 'elf'; New-Item -ItemType Directory -Force $elfDir | Out-Null
$elf = Join-Path $build 'ffxiii2'
if (Test-Path $elf) { Copy-Item -Force $elf (Join-Path $elfDir ("ffxiii2_{0:yyyyMMdd_HHmm}.elf" -f (Get-Date))) }
Write-Host 'Build done. Next: powershell -ExecutionPolicy Bypass -File tools\win\package_switch.ps1' -ForegroundColor Green
