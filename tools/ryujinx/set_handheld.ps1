# ffxiii2-nx (2026-11-08, the user: emulator tests in the Switch's handheld mode): Ryujinx's docked_mode -> false
# (-Docked: back to true). The first run keeps a copy, %APPDATA%\Ryujinx\Config.json.before_handheld.
param([switch]$Docked)
$c = Join-Path $env:APPDATA 'Ryujinx\Config.json'
if (-not (Test-Path ($c + '.before_handheld'))) { Copy-Item $c ($c + '.before_handheld') }
$t = [IO.File]::ReadAllText($c)
$want = if ($Docked) { 'true' } else { 'false' }
$t = [regex]::Replace($t, '("docked_mode"\s*:\s*)(true|false)', ('${1}' + $want))
[IO.File]::WriteAllText($c, $t)
Select-String -Path $c -Pattern 'docked_mode'
