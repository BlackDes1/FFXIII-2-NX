# FFXIII-2 port: installs everything the Windows build needs, then runs tools\win\setup.ps1.
#   Right-click > Run with PowerShell, or:  powershell -ExecutionPolicy Bypass -File tools\win\install_prereqs.ps1
# Installs (skipping what is already there):
#   Git, Python 3.12 (on PATH)                     - via winget
#   VS 2022 Build Tools: C++ workload + Windows SDK - via winget (fallback: Microsoft's bootstrapper)
#   LLVM clang 20.1.8 -> Works\_tools\llvm          - official installer from GitHub, not added to PATH
# Asks for administrator rights once (the installers need them).
param([switch]$NoSetup)
$ErrorActionPreference = 'Stop'

# --- run as administrator ---------------------------------------------------------------------------------------
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
  [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) {
  $a = @('-NoExit', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"")
  if ($NoSetup) { $a += '-NoSetup' }
  Start-Process powershell -Verb RunAs -ArgumentList $a
  exit
}

$repo  = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$works = Split-Path $repo -Parent
$dl    = Join-Path $works '_tools\downloads'; New-Item -ItemType Directory -Force $dl | Out-Null
function Step($t) { Write-Host "`n== $t" -ForegroundColor Cyan }
function Refresh-Path {
  $env:PATH = [Environment]::GetEnvironmentVariable('PATH', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('PATH', 'User')
}
function Get-File($url, $out) {
  if (-not (Test-Path $out)) {
    Write-Host "downloading $url"; $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest $url -OutFile "$out.part" -UseBasicParsing
    Move-Item "$out.part" $out -Force   # only a complete download gets the final name
  }
  return $out
}
$winget = [bool](Get-Command winget -ErrorAction SilentlyContinue)
if (-not $winget) { Write-Warning 'winget not found (install "App Installer" from the Microsoft Store). Git and Python must then be installed by hand.' }
function Winget($id, $override) {
  $wa = @('install', '--id', $id, '-e', '--source', 'winget', '--accept-package-agreements', '--accept-source-agreements', '--silent')
  if ($override) { $wa += @('--override', $override) }
  & winget @wa
  # 0 = installed; -1978335189 (0x8A15002B) = already installed / no newer version
  if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne -1978335189) { Write-Warning "winget $id returned $LASTEXITCODE" }
}

# --- Git ----------------------------------------------------------------------------------------------------------
Step 'Git'
if (Get-Command git -ErrorAction SilentlyContinue) { git --version } elseif ($winget) { Winget 'Git.Git' }

# --- Python -------------------------------------------------------------------------------------------------------
Step 'Python 3'
$py = Get-Command python -ErrorAction SilentlyContinue
if ($py -and $py.Source -notlike '*WindowsApps*') { python --version }   # WindowsApps\python.exe is only the Store alias
elseif ($winget) { Winget 'Python.Python.3.12' '/quiet InstallAllUsers=1 PrependPath=1 Include_launcher=1' }

# --- VS 2022 Build Tools ------------------------------------------------------------------------------------------
Step 'VS 2022 Build Tools (C++ workload + Windows SDK)'
$vs = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
$vsArgs = '--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
if (Test-Path (Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat')) { Write-Host "found: $vs" }
else {
  if ($winget) { Winget 'Microsoft.VisualStudio.2022.BuildTools' $vsArgs }
  if (-not (Test-Path (Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'))) {
    $boot = Get-File 'https://aka.ms/vs/17/release/vs_BuildTools.exe' (Join-Path $dl 'vs_BuildTools.exe')
    Start-Process $boot -ArgumentList $vsArgs -Wait
  }
}

# --- LLVM clang 20 (portable location, not on PATH) ---------------------------------------------------------------
Step 'LLVM clang 20.1.8 -> Works\_tools\llvm'
$llvm = Join-Path $works '_tools\llvm'
if (Test-Path (Join-Path $llvm 'bin\clang++.exe')) { & (Join-Path $llvm 'bin\clang++.exe') --version | Select-Object -First 1 }
else {
  $exe = Get-File 'https://github.com/llvm/llvm-project/releases/download/llvmorg-20.1.8/LLVM-20.1.8-win64.exe' (Join-Path $dl 'LLVM-20.1.8-win64.exe')
  # NSIS installer: /S silent, /D= target folder (must be the last argument, unquoted)
  Start-Process $exe -ArgumentList "/S /D=$llvm" -Wait
  if (-not (Test-Path (Join-Path $llvm 'bin\clang++.exe'))) { throw "LLVM install failed: $llvm\bin\clang++.exe missing" }
}

# --- check ----------------------------------------------------------------------------------------------------------
Step 'Check'
Refresh-Path
$ok = $true
foreach ($t in @(@('git', (Get-Command git -ErrorAction SilentlyContinue)),
                 @('python', (Get-Command python -ErrorAction SilentlyContinue | Where-Object Source -notlike '*WindowsApps*')),
                 @('VS Build Tools', (Test-Path (Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'))),
                 @('clang 20', (Test-Path (Join-Path $llvm 'bin\clang++.exe'))))) {
  if ($t[1]) { Write-Host ("  OK       " + $t[0]) -ForegroundColor Green } else { Write-Host ("  MISSING  " + $t[0]) -ForegroundColor Red; $ok = $false }
}
if (-not $ok) { throw 'Something is missing (see above). Fix it, then run this script again.' }

if (-not $NoSetup) {
  Step 'Running tools\win\setup.ps1'
  & powershell -ExecutionPolicy Bypass -File (Join-Path $repo 'tools\win\setup.ps1')
}
