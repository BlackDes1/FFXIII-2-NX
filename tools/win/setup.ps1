# FFXIII-2 port: Windows setup: toolchain check, sa2-nx SDK fork + third-party sources, game root, rexglue.exe.
#
#   powershell -ExecutionPolicy Bypass -File tools\win\setup.ps1
#
# Needs: Git, Python 3, VS 2022 Build Tools ("Desktop development with C++" + Windows SDK), and LLVM clang 20
# (portable copy at Works\_tools\llvm, or set $env:SA2_LLVM). Everything it writes stays under Works\:
#   Works\sa2-nx       reference checkout of ChanseyIsTheBest/sa2-nx (SDK fork, tools, renderer)
#   Works\game_root    your game files: disc default.xex + z*.xex with the TU6 *.xexp beside them (hard links)
#   ffxiii2-nx\sdk     copy of sa2-nx\sdk (+ third-party sources), ffxiii2-nx\out (build), app\generated (codegen)
# Game files and generated code are git-ignored. Nothing is installed system-wide.
param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$repo  = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$works = Split-Path $repo -Parent
$log   = Join-Path $repo 'out'; New-Item -ItemType Directory -Force $log | Out-Null
function Step($t) { Write-Host "`n== $t" -ForegroundColor Cyan }

Step 'Checking tools'
foreach ($c in 'git','python') { if (-not (Get-Command $c -ErrorAction SilentlyContinue)) { throw "$c not found on PATH" } }
$llvm = if ($env:SA2_LLVM) { $env:SA2_LLVM } else { Join-Path $works '_tools\llvm' }
if (-not (Test-Path "$llvm\bin\clang++.exe")) {
  throw "LLVM clang 20 not found in $llvm. Unpack a portable LLVM 20 (e.g. clang+llvm-20.x-x86_64-pc-windows-msvc) there, or set SA2_LLVM."
}
$ver = & "$llvm\bin\clang++.exe" --version | Select-Object -First 1
Write-Host "clang: $ver"
if ($ver -notmatch 'version 20\.') { Write-Warning 'The sa2-nx SDK fork is built with clang 20; other versions are untested.' }
$vs = if ($env:SA2_VSDIR) { $env:SA2_VSDIR } else { 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools' }
if (-not (Test-Path (Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'))) { throw "VS 2022 Build Tools not found at $vs (set SA2_VSDIR)" }

Step 'sa2-nx reference checkout'
$sa2 = Join-Path $works 'sa2-nx'
if (-not (Test-Path (Join-Path $sa2 '.git'))) { git clone --depth 1 https://github.com/ChanseyIsTheBest/sa2-nx.git $sa2 }
git -C $sa2 log -1 --format='sa2-nx at %h %cs %s'

Step 'SDK (sa2-nx fork) + third-party sources'
$sdk = Join-Path $repo 'sdk'
if (-not (Test-Path (Join-Path $sdk 'CMakeLists.txt'))) {
  robocopy (Join-Path $sa2 'sdk') $sdk /E /NFL /NDL /NJH /NJS /NP | Out-Null
  Copy-Item (Join-Path $sa2 'tools\*.py'), (Join-Path $sa2 'tools\*.sh') (Join-Path $repo 'tools') -ErrorAction SilentlyContinue
}
python (Join-Path $repo 'tools\fetch_thirdparty.py') --target (Join-Path $sdk 'thirdparty')
if ($LASTEXITCODE) { throw 'fetch_thirdparty.py failed' }

Step 'SDK patches (tools\sdk_patches, each with its reason inside)'
foreach ($p in Get-ChildItem (Join-Path $repo 'tools\sdk_patches') -Filter '*.patch' -ErrorAction SilentlyContinue | Sort-Object Name) {
  # cmd /c: Windows PowerShell turns a native command's stderr into an error under ErrorActionPreference=Stop.
  & $env:ComSpec /c "git -C `"$repo`" apply --reverse --check `"$($p.FullName)`" >nul 2>nul"
  if ($LASTEXITCODE -eq 0) { Write-Host "  already applied: $($p.Name)"; continue }
  & $env:ComSpec /c "git -C `"$repo`" apply `"$($p.FullName)`""
  if ($LASTEXITCODE) { throw "SDK patch did not apply: $($p.Name)" }
  Write-Host "  applied: $($p.Name)"
}

Step 'Game root (hard links to your extracted files; nothing is copied into the repository)'
$gr = Join-Path $works 'game_root'; New-Item -ItemType Directory -Force $gr | Out-Null
$disc = Join-Path $works 'game_data\disc'; $tu = Join-Path $works 'game_data\tu'
foreach ($f in @(Get-ChildItem $disc -File) + @(Get-ChildItem $tu -Filter '*.xexp')) {
  $dst = Join-Path $gr $f.Name
  if (-not (Test-Path $dst)) { New-Item -ItemType HardLink -Path $dst -Target $f.FullName | Out-Null }
}
$alba = Join-Path $gr 'alba_data'
if (-not (Test-Path $alba)) { New-Item -ItemType Junction -Path $alba -Target (Join-Path $disc 'alba_data') | Out-Null }
Get-ChildItem $gr | Format-Table Name, Length -AutoSize

. (Join-Path $sa2 'tools\win\env.ps1')   # MSVC libs + clang 20 + CMake/Ninja into this session
$ErrorActionPreference = 'Stop'
$host_out = Join-Path $repo 'out\host'
if (-not $SkipBuild) {
  Step 'Building rexglue (code generator)'
  cmake -S $sdk -B $host_out -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ `
        "-DCMAKE_C_FLAGS=-march=x86-64-v2" "-DCMAKE_CXX_FLAGS=-march=x86-64-v2" *> (Join-Path $log 'host_configure.log')
  if ($LASTEXITCODE) { throw 'configure failed: out\host_configure.log' }
  cmake --build $host_out --target rexglue *> (Join-Path $log 'host_build.log')
  if ($LASTEXITCODE) { throw 'build failed: out\host_build.log' }
}
# The SDK writes its tools to sdk\out\<platform> (e.g. sdk\out\win-amd64\rexglue.exe), not into the build folder.
$rexglue = Get-ChildItem (Join-Path $repo 'sdk\out'), (Join-Path $repo 'out\host') -Recurse -Include rexglue.exe -ErrorAction SilentlyContinue |
  Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
if (-not $rexglue) { throw "rexglue.exe was not built: see out\host_build.log" }
Write-Host "rexglue: $rexglue"

Write-Host "`nSetup done. Next: powershell -ExecutionPolicy Bypass -File tools\win\codegen.ps1" -ForegroundColor Green
