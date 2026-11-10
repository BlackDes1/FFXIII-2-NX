# ffxiii2-nx (2026-11-08, k251): perf_run.ps1 with a screenshot every -Every seconds during the fight (the user's broken
# battle picture: which configuration draws it). Deploys -Nro with -Toml (experimental slot, size-only), starts the game,
# drives it to the battle (nav_ffxiii2.py: loads the selected save tile, never writes a save), presses A every 2.5 s for
# -Seconds and takes a shot every -Every seconds into logs\visual\<Name>\fight_NNN.jpg, closes the game, collects the
# logs and puts -RestoreNro / -Restore back in the current slot. No Frame Tap. Run from ffxiii2-k.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Toml,
    [int]$Seconds = 200,
    [int]$Every = 20,
    [string]$Nro = 'app\out\switch-pgo-use2\renut.nro',
    [string]$Elf = 'app\out\switch-pgo-use2\renut',
    [string]$RestoreNro = 'app\out\switch-pgo-use2\renut.nro',
    [string]$RestoreElf = 'app\out\switch-pgo-use2\renut',
    [string]$Restore = 'dist\switch\ffxiii2-nx\ffxiii2_30fps.toml'
)
$py = 'C:\Python314\python.exe'
New-Item -ItemType Directory -Force ('logs\visual\' + $Name) | Out-Null
$o = (Resolve-Path ('logs\visual\' + $Name)).Path
& $py tools\switch_deploy\deploy.py deploy --nro $Nro --elf $Elf --toml $Toml --cache out\deko3d_shaders\deko3d_shaders_rc9.bin --name $Name --slot experimental --size-only
if ($LASTEXITCODE -ne 0) { 'deploy failed'; exit 1 }
& $py tools\switch_remote\home_launch.py --kill --wait 120
& $py tools\switch_tests\nav_ffxiii2.py --session $Name --timeout 600 *> ($o + '\nav_out.txt')
$navrc = $LASTEXITCODE
Get-Content ($o + '\nav_out.txt') -Tail 4
if ($navrc -eq 0) {
    $t0 = Get-Date
    $end = $t0.AddSeconds($Seconds)
    $nextShot = $t0
    $n = 0
    $k = 0
    while ((Get-Date) -lt $end) {
        if ((Get-Date) -ge $nextShot) {
            $k++
            & $py tools\switch_remote\switch.py shot ($o + '\fight_' + $k.ToString('000') + '.jpg') | Out-Null
            $nextShot = $nextShot.AddSeconds($Every)
        }
        & $py tools\switch_remote\switch.py click A | Out-Null
        $n++
        Start-Sleep -Milliseconds 2500
    }
    'fight presses: ' + $n + ', shots: ' + $k
} else {
    'nav did not reach the battle (exit ' + $navrc + ')'
}
& $py tools\switch_remote\switch.py detach
Start-Sleep -Seconds 5
& $py tools\switch_remote\switch.py kill
Start-Sleep -Seconds 5
& $py tools\switch_logs\collect.py --session $Name --no-parse
& $py tools\switch_deploy\deploy.py deploy --nro $RestoreNro --elf $RestoreElf --toml $Restore --cache out\deko3d_shaders\deko3d_shaders_rc9.bin --name ($Name + '_restore') --slot current --size-only
'shots_run done'
