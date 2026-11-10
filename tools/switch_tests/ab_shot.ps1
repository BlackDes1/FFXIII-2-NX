# ffxiii2-nx (2026-11-08, k255): one A/B picture: deploy -Nro/-Elf/-Toml (experimental slot), start the game (closes a
# running one), nav_ffxiii2.py to the loaded save, -Wait seconds standing still, three shots 2 s apart into
# logs\visual\<Name>\, close the game, collect the logs. No presses after nav (the scene stays put). Run from ffxiii2-k.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Nro,
    [Parameter(Mandatory = $true)][string]$Elf,
    [Parameter(Mandatory = $true)][string]$Toml,
    [int]$Wait = 25,
    # k255b: A pressed every 2.5 s for this many seconds after the wait, a shot every 10 s meanwhile (the grain showed
    # after k254_check's 60 s of presses, not in a still scene)
    [int]$PressA = 0
)
$py = 'C:\Python314\python.exe'
New-Item -ItemType Directory -Force ('logs\visual\' + $Name) | Out-Null
$o = (Resolve-Path ('logs\visual\' + $Name)).Path
& $py tools\switch_remote\switch.py kill | Out-Null
Start-Sleep -Seconds 5
& $py tools\switch_deploy\deploy.py deploy --nro $Nro --elf $Elf --toml $Toml --cache out\deko3d_shaders\deko3d_shaders_rc9.bin --name $Name --slot experimental --size-only 2>&1 | Select-Object -Last 2
if ($LASTEXITCODE -ne 0) { 'deploy failed'; exit 1 }
& $py tools\switch_remote\home_launch.py --kill --wait 120
& $py tools\switch_tests\nav_ffxiii2.py --session $Name --timeout 240 *> ($o + '\nav_out.txt')
'nav exit ' + $LASTEXITCODE
Start-Sleep -Seconds $Wait
foreach ($i in 1, 2, 3) { & $py tools\switch_remote\switch.py shot ($o + '\shot_' + $i + '.jpg') | Out-Null; Start-Sleep -Seconds 2 }
if ($PressA -gt 0) {
    $end = (Get-Date).AddSeconds($PressA); $next = Get-Date; $k = 0
    while ((Get-Date) -lt $end) {
        if ((Get-Date) -ge $next) { $k++; & $py tools\switch_remote\switch.py shot ($o + '\press_' + $k.ToString('00') + '.jpg') | Out-Null; $next = $next.AddSeconds(10) }
        & $py tools\switch_remote\switch.py click A | Out-Null
        Start-Sleep -Milliseconds 2500
    }
    & $py tools\switch_remote\switch.py shot ($o + '\press_end.jpg') | Out-Null
}
& $py tools\switch_remote\switch.py detach
Start-Sleep -Seconds 3
& $py tools\switch_remote\switch.py kill
Start-Sleep -Seconds 8
& $py tools\switch_logs\collect.py --session $Name --no-parse
'ab_shot done'
