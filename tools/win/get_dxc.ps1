# ffxiii2-nx (2026-11-03, the user's permission): DXC with SPIR-V (the DirectXShaderCompiler release package) into
# XenosRecomp's dxc-bin (bin\x64, lib\x64, inc), and Python's xxhash module (tools/dump_shaders.py).
# Renut's host shaders are compiled to SPIR-V on every platform; the Windows SDK's DXC has no SPIR-V back end.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$dst = Join-Path $root 'app\thirdparty\reblue-XenosRecomp\thirdparty\dxc-bin'
$tmp = Join-Path $root 'out\dxc_download'
New-Item -ItemType Directory -Force $tmp | Out-Null
$rel = Invoke-RestMethod -UseBasicParsing 'https://api.github.com/repos/microsoft/DirectXShaderCompiler/releases/latest'
$asset = $rel.assets | Where-Object { $_.name -match '^dxc_.*\.zip$' } | Select-Object -First 1
"release $($rel.tag_name): $($asset.name) ($([int]($asset.size / 1MB)) MB)"
$zip = Join-Path $tmp $asset.name
Invoke-WebRequest -UseBasicParsing $asset.browser_download_url -OutFile $zip
"sha256 $((Get-FileHash -Algorithm SHA256 $zip).Hash)"
$x = Join-Path $tmp 'x'
Expand-Archive -Force $zip $x
foreach ($d in 'bin\x64', 'lib\x64') {
  New-Item -ItemType Directory -Force (Join-Path $dst $d) | Out-Null
  Copy-Item -Force (Join-Path $x "$d\*") (Join-Path $dst $d)
}
& (Join-Path $dst 'bin\x64\dxc.exe') --version
'spirv check:'
$hl = Join-Path $tmp 't.hlsl'
Set-Content -Encoding ascii $hl 'float4 main() : SV_Target { return 1; }'
& (Join-Path $dst 'bin\x64\dxc.exe') -T ps_6_0 -E main -spirv -Fo (Join-Path $tmp 't.spv') $hl
"dxc spirv exit $LASTEXITCODE"
python -m pip install xxhash
python -c "import xxhash, sys; print('xxhash', xxhash.VERSION, sys.executable)"
python -c "import PIL; print('Pillow', PIL.__version__)"
