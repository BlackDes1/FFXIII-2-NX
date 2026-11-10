# ffxiii2-nx (2026-11-08, k263/k264): the register-diet audit codegen (tools\ffxiii2_codegen_diet.bat -> out\audit\gen_diet)
# and the read-before-write audit on it; app\generated is not touched. Run from ffxiii2-k.
#   -Tag try2: an earlier out\audit\gen_diet is renamed gen_diet_<previous tag> first (nothing deleted).
param([string]$Tag = 'run')
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
New-Item -ItemType Directory -Force out\audit | Out-Null
if (Test-Path out\audit\gen_diet) { Rename-Item out\audit\gen_diet ('gen_diet_before_' + $Tag) }
if (Test-Path out\audit\zones_diet) { Rename-Item out\audit\zones_diet ('zones_diet_before_' + $Tag) }
$t0 = Get-Date
& (Join-Path $PSScriptRoot 'ffxiii2_codegen_diet.bat') *> ('out\audit\codegen_diet_' + $Tag + '.log')
"diet codegen exit $LASTEXITCODE ($([int]((Get-Date) - $t0).TotalSeconds) s)"
Get-Content out\codegen_diet_z0070.log -Tail 3 -ErrorAction SilentlyContinue
if (Test-Path out\audit\gen_diet) {
  & C:\Python314\python.exe tools\read_before_write.py out\audit\gen_diet --noreturn=82C15900,82C150C8 2>$null > ('out\audit\rbw_' + $Tag + '.txt')
  "read_before_write exit $LASTEXITCODE"
  Get-Content ('out\audit\rbw_' + $Tag + '.txt') | Select-String -NotMatch '^# store-only'
}
