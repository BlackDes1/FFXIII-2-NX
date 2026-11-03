# FFXIII-2 port: runs the job written in out\job.txt (one PowerShell command line per line, run in order from the
# repository folder), so a test can be started by double-clicking run_job.cmd without writing a new .cmd each time.
# The job file is renamed to out\job_<time>.done.txt before it runs (a second double-click does nothing), and the
# output of every line goes to out\job_<time>.log (UTF-8). Do not end an argument with a backslash ("-Sd H:" rather
# than "-Sd H:\"): powershell -Command reads \" as a quote.
$ErrorActionPreference = 'Continue'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $repo
$job = Join-Path $repo 'out\job.txt'
if (-not (Test-Path $job)) { exit 0 }
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$done = Join-Path $repo "out\job_$stamp.done.txt"
Move-Item $job $done
$log = Join-Path $repo "out\job_$stamp.log"
foreach ($line in Get-Content $done) {
  if (-not $line.Trim() -or $line.TrimStart().StartsWith('#')) { continue }
  "== $(Get-Date -Format HH:mm:ss) $line" | Out-File -Append -Encoding utf8 $log
  & powershell -NoProfile -ExecutionPolicy Bypass -Command $line 2>&1 | Out-File -Append -Encoding utf8 $log
  "== exit $LASTEXITCODE" | Out-File -Append -Encoding utf8 $log
  if ($LASTEXITCODE -and $line -match 'build_switch') { break }   # no test run of a build that failed
}
"== $(Get-Date -Format HH:mm:ss) job done" | Out-File -Append -Encoding utf8 $log
