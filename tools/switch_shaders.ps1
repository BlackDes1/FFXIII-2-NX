# katamari-nx: the Switch build's shader inputs from the PC build (banjo-nx's steps, docs/title-update-3.md "Shaders the
# update brings" and docs/deko3d-port-faq.md), on every shader of out/renut_shader_dump (the image's containers and the
# PC runs' captures, which the PC build adds there):
#   1. Renut's XenosRecomp (the PC build's, app/out/pc/xenos_recomp): the early-alpha dump (BANJO_HLSL_DUMP_ONLY),
#      tools/renut_early_alpha.py, then the translation with BANJO_HLSL_OVERRIDE and BANJO_DEKO3D_SPIRV_DIR (SPIR-V per
#      shader);
#   2. tools/deko3d_shaders.py (SPIRV-Cross -> GLSL for uam), tools/deko3d_glsl_opt.py (every pass), then
#      tools/deko3d_shader_package.py -> app/out/renut_switch/deko3d_glsl*.{bin,cpp};
#   3. tools/renut_switch_shader_cache.py --pc app/out/pc -> app/out/renut_switch/shader_cache_switch.cpp.
#   powershell -File tools/switch_shaders.ps1
# katamari-nx (2026-11-02): XenosRecomp samples inputs at the centroid under MSAA as each shader's header asks
# (BANJO_CENTROID, on unless 0). -NoCentroid translates with BANJO_CENTROID=0 (rc9's shaders) into its own folders
# (out/deko3d_shaders_nocentroid, app/out/renut_switch_nocentroid: a GLSL package the runtime loads from the SD,
# /switch/katamari_nx/deko3d_glsl.bin), step 3 left out (the NRO's own shader cache stays the default set's).
# ffxiii2-nx (2026-11-05, the complete shader set; renut-nx-webinstaller dumps every container of the game files the
# same way): -Scan <folder> adds tools/ffxiii2_scan_shaders.py's scan_*.bin (every container in the alba_data
# archives) to the dump, Switch-only: both are copied to out\deko3d_input (the dump's zero-byte scan_* placeholders
# left out), XenosRecomp translates that folder, and step 3 makes the Switch table from this translation
# (renut_switch_shader_cache.py --cpp out\deko3d_shaders\shader_cache_deko3d.cpp --all: every shader, no DXIL or
# SPIR-V) instead of the PC build's, whose table stays the dump's alone.
param([switch]$NoCentroid, [string]$Scan = '')
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$py = if ($env:FFX_PYTHON) { $env:FFX_PYTHON } else { "C:\Python314\python.exe" }  # GitHub package: FFX_PYTHON
$exe = Join-Path $root "app\out\pc\xenos_recomp\build\XenosRecomp\XenosRecomp.exe"
$common = Join-Path $root "app\thirdparty\reblue-XenosRecomp\XenosRecomp\shader_common.h"
$dump = Join-Path $root "out\renut_shader_dump"
$work = Join-Path $root $(if ($NoCentroid) { "out\deko3d_shaders_nocentroid" } else { "out\deko3d_shaders" })
$pkg = $(if ($NoCentroid) { "app\out\renut_switch_nocentroid" } else { "app\out\renut_switch" })
if ($NoCentroid) { $env:BANJO_CENTROID = "0" } else { Remove-Item Env:\BANJO_CENTROID -ErrorAction SilentlyContinue }
if ($Scan) {
    $inDir = Join-Path $root "out\deko3d_input"
    if (Test-Path $inDir) { Remove-Item -Recurse -Force $inDir }
    New-Item -ItemType Directory -Force $inDir | Out-Null
    $nDump = 0; $nScan = 0
    Get-ChildItem $dump -File | Where-Object { $_.Length -gt 0 } | ForEach-Object { Copy-Item $_.FullName $inDir; $nDump++ }
    Get-ChildItem (Resolve-Path $Scan).Path -File -Filter 'scan_*.bin' | Where-Object { $_.Length -gt 0 } | ForEach-Object { Copy-Item -Force $_.FullName $inDir; $nScan++ }
    "switch_shaders -Scan: $nDump dump files + $nScan scan files -> $inDir"
    $dump = $inDir
    $env:BANJO_ALLOW_SHADER_FAILURES = "1"   # XenosRecomp: a shader DXC refuses is left out (listed), not fatal
}
$early = Join-Path $work "early_alpha_hlsl"
$spirv = Join-Path $work "spirv"
foreach ($d in @($early, $spirv)) { if (Test-Path $d) { Remove-Item -Recurse -Force $d }; New-Item -ItemType Directory -Force $d | Out-Null }
if (-not (Test-Path $exe)) { throw "no ${exe}: build the PC app with Renut first" }
$env:BANJO_HLSL_DUMP_ONLY = "1"
& $exe $dump (Join-Path $work "early_alpha_pass1.cpp") $common $early
if ($LASTEXITCODE -ne 0) { throw "XenosRecomp (early-alpha dump) failed" }
Remove-Item Env:\BANJO_HLSL_DUMP_ONLY
& $py (Join-Path $root "tools\renut_early_alpha.py") $early
if ($LASTEXITCODE -ne 0) { throw "renut_early_alpha.py failed" }
$env:BANJO_HLSL_OVERRIDE = $early
$env:BANJO_DEKO3D_SPIRV_DIR = $spirv
& $exe $dump (Join-Path $work "shader_cache_deko3d.cpp") $common
if ($LASTEXITCODE -ne 0) { throw "XenosRecomp (deko3d SPIR-V) failed" }
Remove-Item Env:\BANJO_HLSL_OVERRIDE; Remove-Item Env:\BANJO_DEKO3D_SPIRV_DIR
Push-Location $root
try {
    & $py tools\deko3d_shaders.py --spirv $spirv --out $work
    if ($LASTEXITCODE -ne 0) { throw "deko3d_shaders.py failed" }
    & $py tools\deko3d_glsl_opt.py --in (Join-Path $work "glsl") --out (Join-Path $work "glsl_opt")
    if ($LASTEXITCODE -ne 0) { throw "deko3d_glsl_opt.py failed" }
    & $py tools\deko3d_shader_package.py --glsl (Join-Path $work "glsl_opt") --out $pkg
    if ($LASTEXITCODE -ne 0) { throw "deko3d_shader_package.py failed" }
    if ($Scan) {
        # ffxiii2-nx (2026-11-05): the complete package (24,920 shaders, 80 MB) goes beside the NRO on the SD card
        # (deko3d_glsl.bin, read on use by banjo_renut_deko3d.cpp); the NRO embeds only the dump's shaders.
        Copy-Item -Force (Join-Path $root "$pkg\deko3d_glsl.bin") (Join-Path $work "deko3d_glsl_full.bin")
        & $py tools\deko3d_shader_package.py --glsl (Join-Path $work "glsl_opt") --out $pkg --only (Join-Path $root "out\renut_shader_dump")
        if ($LASTEXITCODE -ne 0) { throw "deko3d_shader_package.py --only failed" }
        & $py tools\renut_switch_shader_cache.py --cpp (Join-Path $work "shader_cache_deko3d.cpp") --all
        if ($LASTEXITCODE -ne 0) { throw "renut_switch_shader_cache.py --all failed" }
    } elseif (-not $NoCentroid) {
        & $py tools\renut_switch_shader_cache.py --pc app\out\pc
        if ($LASTEXITCODE -ne 0) { throw "renut_switch_shader_cache.py failed" }
    }
} finally { Pop-Location; Remove-Item Env:\BANJO_CENTROID -ErrorAction SilentlyContinue }
Get-ChildItem (Join-Path $root $pkg) | Select-Object Name, Length
