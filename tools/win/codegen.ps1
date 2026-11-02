# FFXIII-2 port: translate default.xex + the four zone DLLs (TU6 applied) to C++ in app\generated.
#   powershell -ExecutionPolicy Bypass -File tools\win\codegen.ps1
# One zone module per run (--target): all four load at 0x88800000, and analysing them in one run maps them over
# each other (bogus "target not in any function"). tools\local\post_codegen_modules.py then lists all four.
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$log = Join-Path $repo 'out'; New-Item -ItemType Directory -Force $log | Out-Null
# The SDK writes its tools to sdk\out\<platform> (e.g. sdk\out\win-amd64\rexglue.exe), not into the build folder.
$rexglue = Get-ChildItem (Join-Path $repo 'sdk\out'), (Join-Path $repo 'out\host') -Recurse -Include rexglue.exe -ErrorAction SilentlyContinue |
  Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
if (-not $rexglue) { throw 'rexglue.exe not found: run tools\win\setup.ps1 first' }
$env:REX_MAX_JUMP_TABLE_ENTRIES = '1024'
$env:REXGLUE_DUMP_IMAGE = Join-Path $log 'default_image.bin'
Push-Location (Join-Path $repo 'app')
try {
  foreach ($z in 'z0070', 'z0130', 'z0160', 'z0161') {
    Write-Host "== codegen default.xex + $z.xex"
    & $env:ComSpec /c "`"$rexglue`" codegen ffxiii2_manifest.toml --target $z --ignore-stamp > `"$log\codegen_$z.log`" 2>&1"
    if ($LASTEXITCODE) { Get-Content "$log\codegen_$z.log" -Tail 30; throw "codegen failed for $z (out\codegen_$z.log)" }
    Get-Content "$log\codegen_$z.log" | Select-String 'Codegen summary'
  }
} finally { Pop-Location }
python (Join-Path $repo 'tools\local\post_codegen_modules.py') (Join-Path $repo 'app')
if ($LASTEXITCODE) { throw 'post_codegen_modules.py failed' }
Write-Host "`nCodegen done. Next: powershell -ExecutionPolicy Bypass -File tools\win\build.ps1" -ForegroundColor Green
