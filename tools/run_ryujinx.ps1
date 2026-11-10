# katamari-nx: one emulator run of the NRO in the user's patched Ryubing with this port's own data folder
# (docs/porting-start-here.md of banjo-nx: "Build and test essentials"). Started minimised; refuses to start while any
# Ryujinx runs or when the free commit memory is under -MinCommitGB; stops only the process it started.
#   powershell -File tools/run_ryujinx.ps1 -Seconds 300 -Name emu1 [-Toml <a katamari.toml with test keys>]
param([int]$Seconds = 240, [string]$Name = "emu", [string]$Toml = "", [double]$MinCommitGB = 12)
$root = Split-Path -Parent $PSScriptRoot
$ryu = "D:\New folder\Ryubing-src\ryubing\build\Ryujinx.exe"
$data = "D:\New folder\ryu-data\katamari"
$sd = Join-Path $data "sdcard\switch\katamari-nx"
$nro = Join-Path $sd "katamari-nx.nro"
$out = Join-Path $root "out\emu_runs\$Name"
if (Get-Process -Name Ryujinx -ErrorAction SilentlyContinue) { Write-Output "a Ryujinx is running (another port's?): not starting"; exit 2 }
$os = Get-CimInstance Win32_OperatingSystem
$freeCommit = ($os.TotalVirtualMemorySize - ($os.TotalVirtualMemorySize - $os.FreeVirtualMemory)) / 1MB
if ($freeCommit -lt $MinCommitGB) { Write-Output ("free commit memory {0:N1} GB < {1} GB: not starting" -f $freeCommit, $MinCommitGB); exit 3 }
if (-not (Test-Path $nro)) { Write-Output "no NRO at $nro"; exit 4 }
if ($Toml) { Copy-Item $Toml (Join-Path $sd "katamari.toml") -Force }
New-Item -ItemType Directory -Force $out | Out-Null
$logs = Join-Path $sd "logs"
if (Test-Path $logs) { Remove-Item -Recurse -Force $logs }
$env:DOTNET_ROOT = "D:\New folder\dotnet10"
# katamari-nx (2026-10-31): the run's own start time ($p.StartTime filtered nothing: every run copied every capture
# the SD had ever kept, ~800 MB a run, 19 GB in out/emu_runs).
$started = Get-Date
$p = Start-Process -FilePath $ryu -ArgumentList @("-r", "`"$data`"", "`"$nro`"") -PassThru -WindowStyle Minimized `
     -RedirectStandardOutput (Join-Path $out "ryujinx_stdout.txt") -RedirectStandardError (Join-Path $out "ryujinx_stderr.txt")
Write-Output ("started Ryujinx pid {0} (free commit {1:N1} GB)" -f $p.Id, $freeCommit)
$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline -and -not $p.HasExited) { Start-Sleep -Seconds 3 }
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; Write-Output "stopped pid $($p.Id) after $Seconds s" }
else { Write-Output "Ryujinx exited by itself (code $($p.ExitCode))" }
Start-Sleep -Seconds 2
if (Test-Path $logs) { Copy-Item -Recurse $logs (Join-Path $out "logs") -Force }
# This run's captures move out of the SD (the SD's folder no longer grows); older ones there are left alone.
Get-ChildItem $sd -Filter "*.png" -Recurse -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -ge $started } |
    Move-Item -Destination $out -Force
Get-ChildItem $out -Recurse | Select-Object FullName, Length
