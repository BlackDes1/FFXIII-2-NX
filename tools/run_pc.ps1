# katamari-nx: one PC test run of app/out/pc/renut.exe: minimised, muted, its own log, PNG captures every N seconds
# (nfsmw_captura_cada_s), stopped after -Seconds. Stops only the process it started.
#   powershell -File tools/run_pc.ps1 -Seconds 120 -Name boot1 [-Every 15] [-Extra "--gpu_plugin=xenos --foo=1"]
# The log is written unbuffered (log_file_writer = false): the run is stopped by force, which would drop the buffer.
# -Visible: the window is shown (not minimised): Renut presents (and so captures and paces by vsync) only with a
# swapchain, which a minimised window does not get.
# ffxiii2-nx (2026-11-04): not muted unless -Mute. Every scripted run used to pass --audio_mute=true, so a run (play8,
# whose capture went into the bug report) was silent although the game delivers its menu music (tile1's audio dumps).
# ffxiii2-nx (2026-11-06): -Max, the most capture ticks (nfsmw_captura_max; a CLI key may be given once only, so not
# through -Extra).
# ffxiii2-nx (2026-11-06): -UserData, the user data folder under out\ (default pc_user). The user's own play saved a new
# slot in pc_user (ff13-14.dat), so the scripted runs, which select Continue, loaded it: the tests use pc_user_test (a
# copy of pc_user without that slot).
param([int]$Seconds = 90, [string]$Name = "run", [int]$Every = 15, [string]$Extra = "", [switch]$Visible, [switch]$Mute,
      [int]$Max = 60, [string]$UserData = "pc_user")
$root = Split-Path -Parent $PSScriptRoot
# ffxiii2-nx: the game files of this PC (Works\game_root: disc + TU6 .xexp; Works\game_data\tu: TU6 data files).
$works = Split-Path -Parent $root
$gameRoot = (Join-Path $works "game_root").Replace("\", "/")
$updateRoot = (Join-Path $works "game_data\tu").Replace("\", "/")
$dir = Join-Path $root "app\out\pc"
$exe = Join-Path $dir "renut.exe"
$logs = Join-Path $root "out\pc_runs\$Name"
New-Item -ItemType Directory -Force $logs | Out-Null
$log = Join-Path $logs "$Name.log"
Remove-Item $log -ErrorAction SilentlyContinue
$min = if ($Visible) { "false" } else { "true" }
$muteArg = if ($Mute) { "true" } else { "false" }  # not $mute: PowerShell names ignore case ($Mute is the switch)
$args = @("--log_file=$log", "--audio_mute=$muteArg", "--window_start_minimized=$min", "--fullscreen=false",
          "--game_data_root=$gameRoot", "--update_data_root=$updateRoot", "--user_data_root=$root/out/$UserData",
          "--nfsmw_captura_cada_s=$Every", "--nfsmw_captura_max=$Max", "--log_file_writer=false") +
        @($Extra -split ' ' | Where-Object { $_ })
# ffxiii2-nx: Start-Process joins the arguments with spaces; this PC's paths have one ("Neuer Ordner"), so every
# argument with a space is quoted (boot1 wrote no log: --log_file was cut at the space).
$args = $args | ForEach-Object { if ($_ -match ' ') { '"' + $_ + '"' } else { $_ } }
$style = if ($Visible) { "Normal" } else { "Minimized" }
$p = Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory $dir -PassThru -WindowStyle $style `
     -RedirectStandardError (Join-Path $logs "stderr.txt") -RedirectStandardOutput (Join-Path $logs "stdout.txt")
Write-Output "started pid $($p.Id)"
$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline -and -not $p.HasExited) { Start-Sleep -Seconds 2 }
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; Write-Output "stopped pid $($p.Id) after $Seconds s" }
else { Write-Output "exited by itself with code $($p.ExitCode)" }
Start-Sleep -Seconds 1
# The captures land next to the executable (nfsmw_nativo_captura.cpp); move this run's into its folder.
Get-ChildItem $dir -Filter "*.png" -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -gt $p.StartTime } |
    Move-Item -Destination $logs -Force -ErrorAction SilentlyContinue
Get-ChildItem (Join-Path $dir "capturas") -Filter "*.png" -ErrorAction SilentlyContinue |
    Where-Object { $_.LastWriteTime -gt $p.StartTime } | Move-Item -Destination $logs -Force -ErrorAction SilentlyContinue
if (Test-Path $log) { Get-Content $log -Tail 25 }
