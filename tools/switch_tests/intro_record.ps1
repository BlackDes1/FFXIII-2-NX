# ffxiii2-nx (2026-11-07, k239): the opening on the console, recorded with Frame Tap once the real-time part runs.
#   powershell -File tools\switch_tests\intro_record.ps1 -Name k239m -Toml dist\switch\ffxiii2-nx\renut_intro_noncpu_off.toml [-Package ..\builds\<rcN>] [-Nro app\out\switch\renut.nro]
# Deploys (experimental slot) the package or the NRO with the toml, starts the game (home_launch.py), runs
# intro_ffxiii2.py (title menu by the screen, NEW GAME: a new save slot, never an existing one; the movie skipped), and
# attaches Frame Tap 8 s after the script's last press (96 s): k239j, the stream attached through the opening's load,
# ended in "the software was closed because an error occurred" (D-006). 45 s recorded to
# logs\visual\<Name>\intro.mp4, the pass counts to out\<Name>\, the logs collected, then rc8 deployed back.
# Run from ffxiii2-k. Never two of these at once, never during a build.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Toml,
    [string]$Package = '',
    [string]$Nro = '',
    [string]$Restore = '..\builds\2026-11-07_0559_ffxiii2_rc8',
    [int]$Seconds = 45
)
$py = 'C:\Python314\python.exe'
if ($Package) {
    & $py tools\switch_deploy\deploy.py deploy --package $Package --toml $Toml --name $Name --slot experimental --size-only
} else {
    & $py tools\switch_deploy\deploy.py deploy --nro $Nro --elf ($Nro -replace '\.nro$', '') --toml $Toml --cache out\deko3d_shaders\deko3d_shaders_rc9.bin --name $Name --slot experimental --size-only
}
if ($LASTEXITCODE -ne 0) { 'deploy failed'; exit 1 }
& $py tools\switch_remote\home_launch.py --kill --wait 120
New-Item -ItemType Directory -Force ("logs\visual\" + $Name) | Out-Null
$o = (Resolve-Path ("logs\visual\" + $Name)).Path
$s = Start-Process -FilePath $py -ArgumentList 'tools\switch_tests\intro_ffxiii2.py', '--out', ("out\" + $Name), '--timeout', '240', '--after', '150', '--force-presses' -RedirectStandardOutput ($o + '\script_out.txt') -RedirectStandardError ($o + '\script_err.txt') -PassThru -WindowStyle Hidden
for ($i = 0; $i -lt 400; $i++) {
    Start-Sleep -Seconds 1
    if ((Test-Path ($o + '\script_out.txt')) -and ((Get-Content ($o + '\script_out.txt') -Raw) -match 't +96\.')) { break }
    if ($s.HasExited) { break }
}
'last press seen after ' + $i + ' s'
Start-Sleep -Seconds 8
$v = (Resolve-Path '..\_tools\switch-frame-tap\switch-frame-tap-0.7.21.1-windows-viewer\SwitchFrameTap.exe').Path
$p = Start-Process -FilePath $v -WorkingDirectory $o -ArgumentList '--headless', '--mp4', 'intro.mp4', '--seconds', "$Seconds" -RedirectStandardOutput ($o + '\viewer_out.txt') -RedirectStandardError ($o + '\viewer_err.txt') -PassThru -WindowStyle Hidden
if (-not $p.WaitForExit(($Seconds + 35) * 1000)) { 'viewer still running: stopping PID ' + $p.Id; Stop-Process -Id $p.Id -Force }
if (-not $s.WaitForExit(120000)) { 'script still running: stopping'; Stop-Process -Id $s.Id -Force }
Get-Content ($o + '\script_out.txt') -Tail 16
Get-Content ($o + '\viewer_out.txt') -Tail 3
& $py tools\switch_remote\switch.py get /switch/ffxiii2-nx/capturas/pass_counts.txt ("out\" + $Name + "\pass_counts.txt")
& $py tools\switch_remote\switch.py detach
Start-Sleep -Seconds 20
& $py tools\switch_remote\switch.py kill
Start-Sleep -Seconds 5
& $py tools\switch_logs\collect.py --session ($Name + '_rec') --no-parse
& $py tools\switch_deploy\deploy.py deploy --package $Restore --name rc8 --slot current --size-only
'intro_record done'
