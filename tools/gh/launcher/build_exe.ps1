# Builds FFXIII-2-NX-Builder.exe: the builder window and everything it needs from this repository in one .exe.
#
#   powershell -ExecutionPolicy Bypass -File tools\gh\launcher\build_exe.ps1 [-Out <file.exe>]
#
# Needs git (the payload is "git archive" of HEAD: committed files only, nothing the build made, no game data) and the
# .NET Framework 4 compiler that every Windows 10/11 has (C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe).
# The .exe (about 125 MB) goes to the repository's folder by default; it is ignored by git and not part of the
# repository. Its version stamp is HEAD's commit time + hash: a started .exe never overwrites a folder that already
# holds the same or a newer builder.
param([string]$Out = '')

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
if (-not $Out) { $Out = Join-Path $repo 'FFXIII-2-NX-Builder.exe' }
$Out = [IO.Path]::GetFullPath($Out)
$fw = Join-Path $env:SystemRoot 'Microsoft.NET\Framework64\v4.0.30319'
$csc = Join-Path $fw 'csc.exe'
if (-not (Test-Path $csc)) { throw "the .NET Framework 4 compiler is missing: $csc" }
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw 'git is needed (the payload is git archive of HEAD)' }

$hash = (& git -C $repo rev-parse --short=8 HEAD).Trim()
$time = (& git -C $repo log -1 --format=%cd --date=format:%Y%m%d%H%M HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or -not $hash -or $time -notmatch '^\d{12}$') { throw 'git could not read HEAD' }
# core.autocrlf=input / core.filemode=false: a checkout made by another git (Git for Windows vs. MSYS2's) is not
# reported as changed only because of line endings or file modes.
if (& git -C $repo -c core.autocrlf=input -c core.filemode=false status --porcelain --untracked-files=no) { Write-Warning 'uncommitted changes: the .exe holds HEAD only (commit them first to include them)' }

$tmp = Join-Path ([IO.Path]::GetTempPath()) ('ffxiii2nx_exe_' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Force $tmp | Out-Null
try {
    $payload = Join-Path $tmp 'payload.zip'
    & git -C $repo archive --format=zip -9 -o $payload HEAD
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $payload)) { throw 'git archive failed' }
    $src = Join-Path $tmp 'Launcher.cs'
    $code = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'Launcher.cs')).Replace('@VERSION@', "$time $hash")
    [IO.File]::WriteAllText($src, $code)
    $icon = Join-Path $repo 'app\icon\app.ico'
    Push-Location $tmp
    try {
        & $csc /nologo /target:winexe /optimize+ /platform:anycpu "/out:$Out" "/win32icon:$icon" /resource:payload.zip,payload.zip `
            /r:System.dll /r:System.Drawing.dll /r:System.Windows.Forms.dll /r:System.Management.dll `
            "/r:$fw\System.IO.Compression.dll" "/r:$fw\System.IO.Compression.FileSystem.dll" Launcher.cs
        if ($LASTEXITCODE -ne 0) { throw "csc failed ($LASTEXITCODE)" }
    } finally { Pop-Location }
} finally {
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
}
$f = Get-Item $Out
Write-Host ("{0}  ({1:N1} MB, builder {2} {3}, SHA-256 {4})" -f $f.FullName, ($f.Length / 1MB), $time, $hash, (Get-FileHash $Out -Algorithm SHA256).Hash)
