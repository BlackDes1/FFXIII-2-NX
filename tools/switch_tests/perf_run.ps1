# ffxiii2-nx (2026-11-07, k245): a measurement run on the console (the 30 fps work, default handheld clocks).
#   powershell -File tools\switch_tests\perf_run.ps1 -Name k245a -Toml dist\switch\ffxiii2-nx\renut_perf_gpu.toml [-Seconds 150]
# Deploys the current NRO with -Toml (experimental slot, size-only), starts the game (home_launch.py --kill), drives it
# to the battle by the screen (nav_ffxiii2.py: loads the selected save tile, never writes a save), then presses A
# (Auto-battle) every 2.5 s for -Seconds so the battle is fought (not idle into GAME OVER), detaches the virtual
# controller, closes the game, collects the logs and puts rc8's ffxiii2.toml back (current slot). No Frame Tap.
# Run from ffxiii2-k. Never two of these at once, never during a build.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Toml,
    [int]$Seconds = 150,
    [string]$Restore = '..\builds\2026-11-07_0559_ffxiii2_rc8\COPY_TO_SD_switch_ffxiii2-nx\ffxiii2.toml',
    # ffxiii2-nx (2026-11-07): another build folder's NRO (the PGO instrumented one, app\out\switch-pgo-gen).
    [string]$Nro = 'app\out\switch\renut.nro',
    [string]$Elf = 'app\out\switch\renut'
)
$py = 'C:\Python314\python.exe'
New-Item -ItemType Directory -Force ("logs\visual\" + $Name) | Out-Null
$o = (Resolve-Path ("logs\visual\" + $Name)).Path
& $py tools\switch_deploy\deploy.py deploy --nro $Nro --elf $Elf --toml $Toml --cache out\deko3d_shaders\deko3d_shaders_rc9.bin --name $Name --slot experimental --size-only
if ($LASTEXITCODE -ne 0) { 'deploy failed'; exit 1 }
& $py tools\switch_remote\home_launch.py --kill --wait 120
& $py tools\switch_tests\nav_ffxiii2.py --session $Name --timeout 600 *> ($o + '\nav_out.txt')
$navrc = $LASTEXITCODE
Get-Content ($o + '\nav_out.txt') -Tail 4
if ($navrc -eq 0) {
    $end = (Get-Date).AddSeconds($Seconds)
    $n = 0
    while ((Get-Date) -lt $end) { & $py tools\switch_remote\switch.py click A | Out-Null; $n++; Start-Sleep -Milliseconds 2500 }
    'fight presses: ' + $n
    & $py tools\switch_remote\switch.py shot ($o + '\end.jpg') | Out-Null
} else {
    'nav did not reach the battle (exit ' + $navrc + ')'
}
& $py tools\switch_remote\switch.py detach
Start-Sleep -Seconds 5
& $py tools\switch_remote\switch.py kill
Start-Sleep -Seconds 5
& $py tools\switch_logs\collect.py --session $Name --no-parse
& $py tools\switch_deploy\deploy.py deploy --nro app\out\switch\renut.nro --elf app\out\switch\renut --toml $Restore --cache out\deko3d_shaders\deko3d_shaders_rc9.bin --name k244 --slot current --size-only
'perf_run done'
