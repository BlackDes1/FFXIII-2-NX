# FFXIII-2 port: build ffxiii2.exe + the zone module DLLs (out\pc), and write the game paths into out\pc\ffxiii2.toml.
#   powershell -ExecutionPolicy Bypass -File tools\win\build.ps1
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$works = Split-Path $repo -Parent
. (Join-Path $works 'sa2-nx\tools\win\env.ps1')
$ErrorActionPreference = 'Stop'
$out = Join-Path $repo 'out\pc'; $log = Join-Path $repo 'out'
cmake -S (Join-Path $repo 'app') -B $out -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ `
      "-DCMAKE_C_FLAGS=-march=x86-64-v2" "-DCMAKE_CXX_FLAGS=-march=x86-64-v2" *> (Join-Path $log 'pc_configure.log')
if ($LASTEXITCODE) { Get-Content (Join-Path $log 'pc_configure.log') -Tail 30; throw 'configure failed: out\pc_configure.log' }
Write-Host '== building (the first build compiles ~190 MB of generated C++: this takes a while)'
cmake --build $out *> (Join-Path $log 'pc_build.log')
if ($LASTEXITCODE) { Get-Content (Join-Path $log 'pc_build.log') -Tail 40; throw 'build failed: out\pc_build.log' }
$toml = Join-Path $out 'ffxiii2.toml'
$t = [IO.File]::ReadAllText($toml)
$t = $t.Replace('@GAME_ROOT@', (Join-Path $works 'game_root').Replace('\', '/')).Replace('@UPDATE_ROOT@', (Join-Path $works 'game_data\tu').Replace('\', '/'))
[IO.File]::WriteAllText($toml, $t)
Get-ChildItem $out -Filter *.exe | Format-Table Name, Length -AutoSize
Get-ChildItem $out -Filter *.dll | Where-Object Name -like 'ffxiii2_z*' | Format-Table Name, Length -AutoSize
Write-Host "Build done. Next: powershell -ExecutionPolicy Bypass -File tools\win\boot_test.ps1" -ForegroundColor Green
