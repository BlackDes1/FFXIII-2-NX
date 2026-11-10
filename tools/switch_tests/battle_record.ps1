# ffxiii2-nx (2026-11-07, k244): the Chaos Bahamut battle on the console, recorded with Frame Tap (D-003 band rate).
#   powershell -File tools\switch_tests\battle_record.ps1 -Name k244a [-Seconds 150]
# Uses the build already on the console (no deploy). Starts the game (home_launch.py --kill), drives it to the battle by
# the screen (nav_ffxiii2.py: reads Save Data 01, never writes a save itself), waits 8 s after the battle is reached,
# then attaches Frame Tap (never during a load: D-006), records $Seconds to logs\visual\<Name>\battle.mp4, detaches the
# virtual controller, waits 20 s (D-005), closes the game and collects the logs.
# Run from ffxiii2-k. Never two of these at once, never during a build.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [int]$Seconds = 150,
    # 2026-11-07: Save Data 01 is a field save now; -NewGame starts NEW GAME (intro_ffxiii2.py: a new, empty save slot,
    # no existing save written) and continues through the opening to the battle (user: "You can Start a New game")
    [switch]$NewGame,
    # 2026-11-07: the user left the game open on the load screen with save 20 (a new game) selected: no launch, nav
    # starts in its "load" phase (A = load the selected tile, A = "Load this data?" yes)
    [switch]$FromLoadScreen
)
$py = 'C:\Python314\python.exe'
New-Item -ItemType Directory -Force ("logs\visual\" + $Name) | Out-Null
$o = (Resolve-Path ("logs\visual\" + $Name)).Path
if (-not $FromLoadScreen) { & $py tools\switch_remote\home_launch.py --kill --wait 120 }
if ($FromLoadScreen) {
    & $py tools\switch_tests\nav_ffxiii2.py --session $Name --timeout 600 --phase load *> ($o + '\nav_out.txt')
} elseif ($NewGame) {
    & $py tools\switch_tests\intro_ffxiii2.py --out ("out\" + $Name + "_intro") --timeout 240 --after 100 *> ($o + '\intro_out.txt')
    Get-Content ($o + '\intro_out.txt') -Tail 4
    & $py tools\switch_tests\nav_ffxiii2.py --session $Name --timeout 600 --phase opening *> ($o + '\nav_out.txt')
} else {
    & $py tools\switch_tests\nav_ffxiii2.py --session $Name --timeout 420 *> ($o + '\nav_out.txt')
}
$navrc = $LASTEXITCODE
Get-Content ($o + '\nav_out.txt') -Tail 6
if ($navrc -ne 0) {
    'nav did not reach the battle (exit ' + $navrc + '): no recording'
    & $py tools\switch_remote\switch.py detach
    exit 2
}
Start-Sleep -Seconds 8
$v = (Resolve-Path '..\_tools\switch-frame-tap\switch-frame-tap-0.7.21.1-windows-viewer\SwitchFrameTap.exe').Path
$p = Start-Process -FilePath $v -WorkingDirectory $o -ArgumentList '--headless', '--mp4', 'battle.mp4', '--seconds', "$Seconds" -RedirectStandardOutput ($o + '\viewer_out.txt') -RedirectStandardError ($o + '\viewer_err.txt') -PassThru -WindowStyle Hidden
if (-not $p.WaitForExit(($Seconds + 35) * 1000)) { 'viewer still running: stopping PID ' + $p.Id; Stop-Process -Id $p.Id -Force }
Get-Content ($o + '\viewer_out.txt') -Tail 3
& $py tools\switch_remote\switch.py detach
Start-Sleep -Seconds 20
& $py tools\switch_remote\switch.py kill
Start-Sleep -Seconds 5
& $py tools\switch_logs\collect.py --session ($Name + '_rec') --no-parse
if (Test-Path ($o + '\battle.mp4')) { 'recorded ' + (Get-Item ($o + '\battle.mp4')).Length + ' bytes' } else { 'no recording' }
'battle_record done'
