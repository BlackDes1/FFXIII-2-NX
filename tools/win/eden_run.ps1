# FFXIII-2 port: one test run of the Switch NRO in Eden (eden-cli), minimized and muted, then its logs collected.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\eden_run.ps1 -Name run1 [-Seconds 240] [-Package]
#
# - The NRO runs from out\nx\sdcard\switch\ffxiii2-nx (tools\win\package_switch.ps1 -LinkGame lays it out; -Package
#   runs it first). Eden's SD card is that sdcard folder: Works\eden_test\ffxiii2-sdl2.ini (a copy of the user's
#   eden-cli config with sdmc_directory pointed there, audio muted, handheld mode, flush_line on) is passed with -c,
#   so the user's own Eden settings are not changed.
# - The window is started minimized and minimized again while it starts (SDL shows its window itself). Only this
#   run's own process is stopped, by process id.
# - Collected into out\eden_runs\<Name>: Eden's log (Eden writes one per start),
#   the SDK's logs from the SD folder (logs\, including logs\rex\rex_crash.log and rex_profile.log) and captures\.
param([Parameter(Mandatory)][string]$Name, [int]$Seconds = 240, [switch]$Package, [ValidateSet('qt', 'cli')][string]$Frontend = 'qt')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$works = Split-Path $repo -Parent
$port = Split-Path $works -Parent
# Frontends: qt = Works\eden_test\eden.exe -g <nro>: a private copy of Port\eden.exe, portable through the user\
# folder beside it (Eden looks for user\ next to the executable; in the working folder it was not used, 2026-11-03),
# so the user's own Eden setup is never read or written. user\config\qt-config.ini: the user's config with
# sdmc_directory pointed at the sdcard folder, muted, handheld, flush_line on, and the debugging options of the
# user's config off (use_gdbstub made the boot wait for a debugger: run 5's log stopped at "Starting server on port
# 6543"); user\keys; the NAND (firmware) stays the user's. cli = eden-cli.exe -c Works\eden_test\ffxiii2-sdl2.ini <nro>
# (2026-11-03: this Eden's eden-cli aborted at start, exit code 3, before writing a log line, with any config).
$edenDir = Join-Path $works 'eden_test'
if ($Frontend -eq 'qt') {
  $eden = Join-Path $works 'eden_test\eden.exe'
  if (-not (Test-Path $eden)) { Copy-Item (Join-Path $port 'eden.exe') $eden }
  $edenLog = Join-Path $edenDir 'user\log\eden_log.txt'
} else {
  $eden = Join-Path $port 'eden-cli.exe'
  $edenLog = Join-Path $env:APPDATA 'eden\log\eden_log.txt'
}
$config = Join-Path $edenDir 'ffxiii2-sdl2.ini'
$sd = Join-Path $repo 'out\nx\sdcard\switch\ffxiii2-nx'
$out = Join-Path $repo "out\eden_runs\$Name"
New-Item -ItemType Directory -Force $out | Out-Null
if ($Package) { & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'package_switch.ps1') -LinkGame | Out-Null }
$nro = Join-Path $sd 'ffxiii2.nro'
if (-not (Test-Path $nro)) { throw "no NRO at $nro (run package_switch.ps1 -LinkGame)" }
# Old SD logs and captures out of the way, so this run's are the only ones collected.
foreach ($d in 'logs', 'captures') {
  $p = Join-Path $sd $d
  if (Test-Path $p) { Rename-Item $p ("{0}_before_{1}" -f $d, $Name) -ErrorAction SilentlyContinue }
}
Add-Type -Namespace W -Name U -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool ShowWindowAsync(System.IntPtr h, int cmd);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, System.IntPtr l);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr h, out uint pid);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(System.IntPtr h);
[DllImport("user32.dll")] public static extern bool IsIconic(System.IntPtr h);
public delegate bool EnumProc(System.IntPtr h, System.IntPtr l);
'@
function Minimize-Process([int]$procId) {
  [W.U]::EnumWindows({ param($h, $l)
      $p = 0; [void][W.U]::GetWindowThreadProcessId($h, [ref]$p)
      if ($p -eq $procId -and [W.U]::IsWindowVisible($h) -and -not [W.U]::IsIconic($h)) { [void][W.U]::ShowWindowAsync($h, 6) }  # SW_MINIMIZE
      $true }, [IntPtr]::Zero) | Out-Null
}
$t0 = Get-Date
"start $t0  eden $eden  config $config  nro $nro  seconds $Seconds" | Set-Content (Join-Path $out 'run.txt')
# eden-cli's own console output (it exits before its log exists when the arguments or the config are wrong) goes to
# eden_stdout.txt / eden_stderr.txt; the window is minimized by Minimize-Process below.
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $eden
$psi.Arguments = if ($Frontend -eq 'qt') { "-g `"$nro`"" } else { "-c `"$config`" `"$nro`"" }
$psi.WorkingDirectory = $edenDir
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$proc = New-Object System.Diagnostics.Process
$proc.StartInfo = $psi
$stdout = New-Object System.Text.StringBuilder; $stderr = New-Object System.Text.StringBuilder
Register-ObjectEvent $proc OutputDataReceived -MessageData $stdout -Action { if ($EventArgs.Data) { [void]$Event.MessageData.AppendLine($EventArgs.Data) } } | Out-Null
Register-ObjectEvent $proc ErrorDataReceived -MessageData $stderr -Action { if ($EventArgs.Data) { [void]$Event.MessageData.AppendLine($EventArgs.Data) } } | Out-Null
[void]$proc.Start(); $proc.BeginOutputReadLine(); $proc.BeginErrorReadLine()
while (((Get-Date) - $t0).TotalSeconds -lt $Seconds -and -not $proc.HasExited) {
  if (((Get-Date) - $t0).TotalSeconds -lt 60) { Minimize-Process $proc.Id }
  Start-Sleep -Milliseconds 500
}
$exited = $proc.HasExited
$code = if ($exited) { $proc.ExitCode } else { $null }
if (-not $exited) { Stop-Process -Id $proc.Id -Force; Start-Sleep -Seconds 2 }
"end $(Get-Date)  ran $([int]((Get-Date) - $t0).TotalSeconds) s  exited by itself: $exited  exit code: $code" | Add-Content (Join-Path $out 'run.txt')
Start-Sleep -Milliseconds 500
$stdout.ToString() | Set-Content (Join-Path $out 'eden_stdout.txt')
$stderr.ToString() | Set-Content (Join-Path $out 'eden_stderr.txt')
Copy-Item -Force $edenLog (Join-Path $out 'eden_log.txt') -ErrorAction SilentlyContinue
foreach ($d in 'logs', 'captures') {
  $p = Join-Path $sd $d
  if (Test-Path $p) { Copy-Item -Recurse -Force $p (Join-Path $out $d) }
}
Get-Content (Join-Path $out 'run.txt')
