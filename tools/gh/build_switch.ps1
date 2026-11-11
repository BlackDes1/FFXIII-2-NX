# ffxiii2-nx: builds the Switch version of Final Fantasy XIII-2 from YOUR OWN game files, on Windows, in one go.
#
#   build_switch.cmd -Iso "D:\Games\Final Fantasy XIII-2 (Europe).iso" -Update "D:\Games\tu0000000b_00000000"
#   build_switch.cmd ... -Dlc "D:\Games\FFXIII-2 DLC"          (optional: a DLC package or a folder of them)
#   (or double-click build_switch.cmd: it asks for the three paths)
#
# Inputs (never copied into this repository, never uploaded anywhere):
#   -Iso     REQUIRED. Your disc image of Final Fantasy XIII-2, Europe (title 53510806), a plain XDVDFS .iso,
#            or a folder with the disc's files (default.xex, alba_data, ...).
#   -Update  REQUIRED. Title update 6 (0.0.6.11): the LIVE package tu0000000b_00000000, or a folder you extracted it into.
#   -Dlc     optional. The Xbox 360 DLC package(s) of the game (a file, or a folder of packages). Without it the game
#            runs without DLC.
#   -Icon    optional. Any picture (PNG, JPEG, BMP, GIF, TIFF, WebP, ICO, TGA, DDS, PSD, AVIF, HEIC, ... of any size)
#            for the NRO's icon in the Homebrew Menu: tools\gh\make_icon.py makes it the 256x256 baseline JPEG the NRO
#            carries (-IconMode fit = whole picture on black, fill = cropped to a square, stretch). Without it: a picture
#            named icon.* in the repository's folder if there is one, else libnx's default icon. Your own picture:
#            never part of this repository (icon.* is ignored by git).
#   -SdCard  optional. Your SD card (e.g. E:) or any folder: the finished switch\ffxiii2-nx folder is copied there
#            (nothing on it is deleted; an ffxiii2.toml already there keeps your settings).
# FFXIII-2-NX Builder.bat opens a window for all of this (tools\gh\builder_gui.ps1).
# Output: <repository>\SD\switch\ffxiii2-nx\  -> copy the "switch" folder to the root of the SD card.
#
# Only the Switch version is built (no PC build, no Visual Studio, no LLVM). Everything it needs is downloaded into a
# tools folder first (portable copies: CMake, Ninja, Python with xxhash and zstandard, Zstandard; devkitPro with
# switch-dev, deko3d, switch-zlib, switch-libzstd when no devkitPro is installed; the Visual C++ runtime when missing).
# The PC tools of the shader chain (the code generator, XenosRecomp, SPIRV-Cross, uam, glslangValidator) are prebuilt
# in tools\gh\bin (built from this repository's sources; tools\gh\bin\README.md).
#
# Steps (each leaves <Work>\steps\<step>.ok; a run continues after the last finished step, -From <step> redoes from
# there): tools, extract, codegen, shaders, cache, switch, package, sdcard, cleanup.
# Disk: about 15 GB free with the game files in the SD folder (8.7 GB of them stay: they go on the SD card), about
# 7 GB with -NoGameFiles; plus about 2.5 GB while devkitPro is installed locally. Every file the build makes besides
# the SD folder is deleted at the end (-KeepWork / -KeepTools keep them).
# It never deletes anything outside this repository's build folders, <Work> and <Tools>; an installed devkitPro is used,
# never removed.
param(
    [string]$Iso = '',
    [string]$Update = '',
    [string]$Dlc = '',
    [string]$Out = '',
    [string]$Work = '',
    [string]$Tools = '',
    [string]$DevkitPro = '',
    [string]$Icon = '',
    [ValidateSet('fit', 'fill', 'stretch')][string]$IconMode = 'fit',
    [string]$SdCard = '',
    [int]$Jobs = 0,
    [switch]$NoGameFiles,
    [switch]$NoPgo,
    [switch]$KeepWork,
    [switch]$KeepTools,
    [switch]$FreshTools,
    [switch]$SkipHashCheck,
    [ValidateSet('', 'tools', 'extract', 'codegen', 'shaders', 'cache', 'switch', 'package', 'sdcard', 'cleanup')][string]$From = ''
)

# A 32-bit PowerShell sees another System32 and cannot run the 64-bit tools well: start the 64-bit one instead.
if (-not [Environment]::Is64BitProcess -and [Environment]::Is64BitOperatingSystem) {
    $ps64 = Join-Path $env:SystemRoot 'Sysnative\WindowsPowerShell\v1.0\powershell.exe'
    $a = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $MyInvocation.MyCommand.Path)
    foreach ($k in $PSBoundParameters.Keys) {
        $v = $PSBoundParameters[$k]
        if ($v -is [switch]) { if ($v) { $a += "-$k" } } else { $a += "-$k"; $a += "$v" }
    }
    & $ps64 @a
    exit $LASTEXITCODE
}

$ErrorActionPreference = 'Stop'
# Windows' own cmd.exe by full path: an MSYS2 'usr\bin' on PATH (devkitPro's installer adds it) has a file named cmd.
$cmdExe = Join-Path $env:SystemRoot 'System32\cmd.exe'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'

function Say($m) { Write-Host "== $m" -ForegroundColor Cyan }
function Note($m) { Write-Host "   $m" }
function Warn($m) { Write-Host "!! $m" -ForegroundColor Yellow }
function Fail($m) { Write-Host "!! $m" -ForegroundColor Red; exit 1 }

# ---- the three inputs (asked for when the script is started without them, e.g. by a double-click) ------------------
if (-not $Iso) {
    Write-Host 'Final Fantasy XIII-2 for the Switch: build from your own game files.'
    $Iso = (Read-Host 'Your disc image (.iso) of Final Fantasy XIII-2 (Europe), or the folder with its files').Trim('"', ' ')
    if (-not $Update) { $Update = (Read-Host 'Title update 6 (the file tu0000000b_00000000, or its folder)').Trim('"', ' ') }
    if (-not $Dlc) { $Dlc = (Read-Host 'DLC package or folder of DLC packages (optional: press Enter to skip)').Trim('"', ' ') }
}
if (-not $Iso -or -not (Test-Path -LiteralPath $Iso)) { Fail "no disc image at '$Iso' (-Iso)" }
if (-not $Update -or -not (Test-Path -LiteralPath $Update)) { Fail "no title update at '$Update' (-Update)" }
if ($Dlc -and -not (Test-Path -LiteralPath $Dlc)) { Fail "no DLC at '$Dlc' (-Dlc; leave it out to build without DLC)" }
if (-not $Icon) {
    $c = Get-ChildItem -LiteralPath $repo -File -Filter 'icon.*' -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -match '^\.(png|jpe?g|bmp|gif|tiff?|webp|ico|tga|dds|psd|avif|heic|heif|jxl|qoi|pcx|ppm|svg)$' } |
        Select-Object -First 1
    if ($c) { $Icon = $c.FullName }
}
if ($Icon -and -not (Test-Path -LiteralPath $Icon -PathType Leaf)) { Fail "no icon picture at '$Icon' (-Icon)" }
if ($Icon) { $Icon = (Resolve-Path -LiteralPath $Icon).Path }
if ($SdCard) {
    if ($SdCard -match '^[A-Za-z]:?$') { $SdCard = $SdCard.Substring(0, 1) + ':\' }
    if (-not (Test-Path -LiteralPath $SdCard -PathType Container)) { Fail "no SD card / folder at '$SdCard' (-SdCard)" }
}
$Iso = (Resolve-Path -LiteralPath $Iso).Path
$Update = (Resolve-Path -LiteralPath $Update).Path
if ($Dlc) { $Dlc = (Resolve-Path -LiteralPath $Dlc).Path }
$isoIsFolder = Test-Path -LiteralPath $Iso -PathType Container

# ---- folders --------------------------------------------------------------------------------------------------------
if (-not $Out) { $Out = Join-Path $repo 'SD' }
if (-not $Work) { $Work = Join-Path $repo 'work' }
if (-not $Tools) {
    # MSYS2 (devkitPro's shell) wants a path without spaces or non-ASCII letters.
    if ($repo -match '^[A-Za-z]:\\[A-Za-z0-9_.\\-]*$') { $Tools = Join-Path $repo '.tools' }
    else { $Tools = $repo.Substring(0, 2) + '\ffxiii2nx_tools' }
}
foreach ($d in @($Out, $Work, $Tools, (Join-Path $Work 'steps'), (Join-Path $repo 'logs'))) { New-Item -ItemType Directory -Force $d | Out-Null }
$Out = (Resolve-Path $Out).Path
$Work = (Resolve-Path $Work).Path
$Tools = (Resolve-Path $Tools).Path
$sd = Join-Path $Out 'switch\ffxiii2-nx'
New-Item -ItemType Directory -Force $sd | Out-Null
$logDir = Join-Path $repo "logs\build_$stamp"
New-Item -ItemType Directory -Force $logDir | Out-Null
$stepNames = @('tools', 'extract', 'codegen', 'shaders', 'cache', 'switch', 'package', 'sdcard', 'cleanup')
$disc = if ($isoIsFolder) { $Iso } elseif ($NoGameFiles) { '' } else { Join-Path $sd 'game_root' }
$tu = if ($NoGameFiles) { Join-Path $Work 'tu' } else { Join-Path $sd 'update' }
if ($From) {
    $i = [array]::IndexOf($stepNames, $From)
    for ($k = $i; $k -lt $stepNames.Count; $k++) { Remove-Item -ErrorAction SilentlyContinue (Join-Path $Work "steps\$($stepNames[$k]).ok") }
}
function Done($s) {
    $f = [math]::Floor((Get-PSDrive $repo.Substring(0, 1)).Free / 1GB)
    Set-Content -Path (Join-Path $Work "steps\$s.ok") -Value "$(Get-Date -Format s) free $f GB"
    Note "$s done $(Get-Date -Format 'HH:mm:ss'), $f GB free on $($repo.Substring(0, 2))"
}
function IsDone($s) { Test-Path (Join-Path $Work "steps\$s.ok") }

# The known-good inputs: the EU disc's default.xex and title update 6's default.xexp.
$XexSha = '645745df61ea4466157dda317e0191424ccd47db46ee10f54c79db340dcdfe3b'
$XexpSha = '8d48ccf8aabff2da2a1da3e4e4315fcb765007ac0c4d293880874f7464224029'
# The compiler the PGO profile (pgo\) was recorded with: another GCC release ignores it, so the build goes without.
$PgoGcc = '16.1'

# ---- helpers --------------------------------------------------------------------------------------------------------
function Sha($p) { (Get-FileHash -Algorithm SHA256 -LiteralPath $p).Hash.ToLower() }
function FreeGb($path) {
    $q = (Resolve-Path $path).Path.Substring(0, 1)
    return [math]::Floor((Get-PSDrive $q).Free / 1GB)
}
function NeedFree($gb, $why) {
    foreach ($p in @($repo, $Work, $Out, $Tools) | Select-Object -Unique) {
        $f = FreeGb $p
        if ($f -lt $gb) { Fail "only $f GB free on the drive of $p; $why needs about $gb GB. Free some space, then run again (it continues where it stopped)." }
    }
}
function Quote([string]$a) {
    if ($a -eq '') { return '""' }
    if ($a -notmatch '[\s"]') { return $a }
    return '"' + (($a -replace '(\\*)"', '$1$1\"') -replace '(\\+)$', '$1$1') + '"'
}
# Runs a program, its output into a log; returns the exit code. -Dir: its working folder.
function Run([string]$log, [string]$exe, [string[]]$argList = @(), [string]$dir = $repo) {
    $tag = [guid]::NewGuid().ToString('N').Substring(0, 8)
    $o = "$log.$tag.out"; $e = "$log.$tag.err"
    $p = Start-Process -FilePath $exe -ArgumentList (($argList | ForEach-Object { Quote $_ }) -join ' ') -WorkingDirectory $dir `
        -NoNewWindow -PassThru -RedirectStandardOutput $o -RedirectStandardError $e
    # Not -Wait: it also waits for every process the program left running (a daemon would block the build forever).
    $null = $p.Handle   # keeps the exit code readable
    $p.WaitForExit()
    $code = $p.ExitCode
    if ($null -eq $code) { $code = -1 }
    foreach ($f in @($o, $e)) { if (Test-Path $f) { Get-Content $f | Add-Content -Path $log; Remove-Item $f -ErrorAction SilentlyContinue } }
    return $code
}
function RunOk([string]$what, [string]$log, [string]$exe, [string[]]$argList = @(), [string]$dir = $repo) {
    $code = Run $log $exe $argList $dir
    if ($code -ne 0) {
        if (Test-Path $log) { Get-Content $log -Tail 25 }
        Fail "$what failed (exit $code): $log"
    }
}
function Py([string]$what, [string]$logName, [string[]]$argList, [string]$dir = $repo) {
    RunOk $what (Join-Path $logDir $logName) $script:python $argList $dir
}
# Deletes a file or folder this build made: only inside the repository's build folders, <Work> or <Tools>.
function Remove-Made([string]$p) {
    if (-not (Test-Path -LiteralPath $p)) { return }
    $full = (Resolve-Path -LiteralPath $p).Path.TrimEnd('\')
    $ok = $false
    foreach ($root in @($Work, $Tools)) { if ($full.StartsWith($root.TrimEnd('\') + '\', 'OrdinalIgnoreCase') -or $full -ieq $root.TrimEnd('\')) { $ok = $true } }
    foreach ($rel in @('out', 'app\out', 'app\generated', 'app\generated_zones', 'sdk\out')) {
        $r = Join-Path $repo $rel
        if ($full -ieq $r -or $full.StartsWith($r + '\', 'OrdinalIgnoreCase')) { $ok = $true }
    }
    if (-not $ok) { Warn "not deleted (outside the build folders): $full"; return }
    $item = Get-Item -LiteralPath $full -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { & $cmdExe /c rmdir "$full" | Out-Null; return }
    Remove-Item -LiteralPath $full -Recurse -Force -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $full) { Warn "could not delete $full (a program still uses it?)" }
}
function Get-Download([string]$url, [string]$dest) {
    if (Test-Path -LiteralPath $dest) { return }
    $tmp = "$dest.part"
    Note "download $url"
    for ($try = 1; $try -le 3; $try++) {
        try {
            $curl = Join-Path $env:SystemRoot 'System32\curl.exe'
            if (Test-Path $curl) {
                & $curl -L --fail --silent --show-error --retry 3 -o $tmp $url
                if ($LASTEXITCODE -ne 0) { throw "curl exit $LASTEXITCODE" }
            } else {
                Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $tmp
            }
            Move-Item -Force $tmp $dest
            return
        } catch {
            Warn "download failed ($try/3): $_"
            Start-Sleep -Seconds (5 * $try)
        }
    }
    Fail "could not download $url (internet connection? a firewall?)"
}
function Expand-Zip([string]$zip, [string]$dest) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    if (Test-Path $dest) { Remove-Made $dest }
    [System.IO.Compression.ZipFile]::ExtractToDirectory($zip, $dest)
}

# ---- 1. tools -------------------------------------------------------------------------------------------------------
$dl = Join-Path $Tools 'downloads'
$python = Join-Path $Tools 'python\tools\python.exe'
$cmake = Join-Path $Tools 'cmake\bin\cmake.exe'
$ninja = Join-Path $Tools 'ninja\ninja.exe'
$zstd = Join-Path $Tools 'zstd\zstd.exe'
$localDkp = Join-Path $Tools 'msys64\opt\devkitpro'
$dkpFile = Join-Path $Tools 'devkitpro.txt'   # which devkitPro the tools step chose

function DkpOk([string]$root) {
    if (-not $root -or -not (Test-Path $root)) { return $false }
    foreach ($f in @('devkitA64\bin\aarch64-none-elf-gcc.exe', 'libnx\lib\libnx.a', 'libnx\lib\libdeko3d.a', 'tools\bin\elf2nro.exe',
                     'tools\bin\nacptool.exe', 'portlibs\switch\lib\libz.a', 'portlibs\switch\lib\libzstd.a')) {
        if (-not (Test-Path (Join-Path $root $f))) { return $false }
    }
    return $true
}
function DkpPacman([string]$msysRoot, [string]$what, [string]$cmd) {
    $bash = Join-Path $msysRoot 'usr\bin\bash.exe'
    $env:MSYSTEM = 'MSYS'; $env:CHERE_INVOKING = '1'
    # gpg-agent / dirmngr (started by pacman-key) stay running and keep the log's handles open, so Start-Process -Wait
    # would wait for them forever: stopped at the end of every command.
    $cmd = "$cmd; rc=`$?; gpgconf --kill all >/dev/null 2>&1; exit `$rc"
    try { RunOk $what (Join-Path $logDir 'devkitpro.log') $bash @('-lc', $cmd) $Tools }
    finally { Remove-Item Env:\MSYSTEM, Env:\CHERE_INVOKING -ErrorAction SilentlyContinue }
}

if (-not (IsDone 'tools')) {
    Say "tools: $Tools"
    NeedFree 3 'the tools'
    New-Item -ItemType Directory -Force $dl | Out-Null
    # The Visual C++ runtime (the prebuilt PC tools need it).
    $sys = Join-Path $env:SystemRoot 'System32'
    if (-not ((Test-Path "$sys\msvcp140.dll") -and (Test-Path "$sys\vcruntime140_1.dll") -and (Test-Path "$sys\msvcp140_atomic_wait.dll"))) {
        Say 'tools: the Microsoft Visual C++ runtime (Windows asks for administrator rights once)'
        $vc = Join-Path $dl 'vc_redist.x64.exe'
        Get-Download 'https://aka.ms/vs/17/release/vc_redist.x64.exe' $vc
        $p = Start-Process -FilePath $vc -ArgumentList '/install /quiet /norestart' -Verb RunAs -Wait -PassThru
        if ($p.ExitCode -notin @(0, 1638, 3010)) { Fail "the Visual C++ runtime did not install (exit $($p.ExitCode)): run $vc yourself" }
    }
    if (-not (Test-Path $cmake)) {
        Say 'tools: CMake 3.31.6'
        $z = Join-Path $dl 'cmake.zip'
        Get-Download 'https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-windows-x86_64.zip' $z
        Expand-Zip $z (Join-Path $Tools 'cmake_x')
        Remove-Made (Join-Path $Tools 'cmake')
        Move-Item (Get-ChildItem (Join-Path $Tools 'cmake_x') -Directory | Select-Object -First 1).FullName (Join-Path $Tools 'cmake')
        Remove-Made (Join-Path $Tools 'cmake_x')
    }
    if (-not (Test-Path $ninja)) {
        Say 'tools: Ninja 1.12.1'
        $z = Join-Path $dl 'ninja.zip'
        Get-Download 'https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip' $z
        Expand-Zip $z (Join-Path $Tools 'ninja')
    }
    if (-not (Test-Path $zstd)) {
        Say 'tools: Zstandard 1.5.7'
        $z = Join-Path $dl 'zstd.zip'
        Get-Download 'https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-v1.5.7-win64.zip' $z
        Expand-Zip $z (Join-Path $Tools 'zstd_x')
        New-Item -ItemType Directory -Force (Join-Path $Tools 'zstd') | Out-Null
        Copy-Item (Get-ChildItem (Join-Path $Tools 'zstd_x') -Recurse -Filter 'zstd.exe' | Select-Object -First 1).FullName $zstd
        Remove-Made (Join-Path $Tools 'zstd_x')
    }
    if (-not (Test-Path $python)) {
        Say 'tools: Python 3.14.8 (portable, from nuget.org)'
        $z = Join-Path $dl 'python.zip'
        Get-Download 'https://api.nuget.org/v3-flatcontainer/python/3.14.8/python.3.14.8.nupkg' $z
        Expand-Zip $z (Join-Path $Tools 'python')
    }
    if ((Run (Join-Path $logDir 'pip.log') $python @('-c', 'import xxhash, zstandard, PIL')) -ne 0) {
        Say 'tools: the Python modules xxhash, zstandard and Pillow (the icon converter)'
        if ((Run (Join-Path $logDir 'pip.log') $python @('-m', 'pip', '--version')) -ne 0) { RunOk 'ensurepip' (Join-Path $logDir 'pip.log') $python @('-m', 'ensurepip', '--default-pip') }
        RunOk 'pip install' (Join-Path $logDir 'pip.log') $python @('-m', 'pip', 'install', '--disable-pip-version-check',
            '--no-warn-script-location', 'xxhash==4.0.2', 'zstandard==0.25.0', 'pillow>=11.3')
        # HEIC / HEIF pictures (phone photos) for the icon: optional, the build goes on without it.
        [void](Run (Join-Path $logDir 'pip.log') $python @('-m', 'pip', 'install', '--disable-pip-version-check',
            '--no-warn-script-location', 'pillow-heif'))
    }
    # devkitPro: an installed one when it has everything, else a local one (MSYS2 + devkitPro's pacman) in <Tools>.
    $dkp = ''
    if (-not $FreshTools) {
        foreach ($c in @($DevkitPro, $env:DEVKITPRO, 'C:\devkitPro')) {
            if ($c -and $c -notmatch '^/') { $c = $c.Replace('/', '\') ; if (DkpOk $c) { $dkp = $c; break } }
        }
        if (-not $dkp) {
            # An installed devkitPro without some package: its own pacman adds them (nothing is removed).
            foreach ($c in @($DevkitPro, $env:DEVKITPRO, 'C:\devkitPro')) {
                if ($c -and $c -notmatch '^/') {
                    $c = $c.Replace('/', '\')
                    if ((Test-Path (Join-Path $c 'devkitA64')) -and (Test-Path (Join-Path $c 'msys2\usr\bin\pacman.exe'))) {
                        Say "tools: adding the missing Switch packages to your devkitPro ($c)"
                        DkpPacman (Join-Path $c 'msys2') 'pacman' 'pacman -Sy --noconfirm && pacman -S --needed --noconfirm switch-dev deko3d switch-zlib switch-libzstd make'
                        if (DkpOk $c) { $dkp = $c; break }
                    }
                }
            }
        }
    }
    if (-not $dkp) {
        if (-not (DkpOk $localDkp)) {
            NeedFree 4 'installing devkitPro'
            $msys = Join-Path $Tools 'msys64'
            if (-not (Test-Path (Join-Path $msys 'usr\bin\bash.exe'))) {
                Say 'tools: MSYS2 (base, for devkitPro''s package manager)'
                $sfx = Join-Path $dl 'msys2-base.sfx.exe'
                Get-Download 'https://github.com/msys2/msys2-installer/releases/download/2026-09-27/msys2-base-x86_64-20260927.sfx.exe' $sfx
                $code = Run (Join-Path $logDir 'msys2.log') $sfx @('-y', "-o$Tools")
                if ($code -ne 0 -or -not (Test-Path (Join-Path $msys 'usr\bin\bash.exe'))) { Fail "unpacking MSYS2 failed ($code): $logDir\msys2.log" }
                Say 'tools: MSYS2 first start (a minute)'
                DkpPacman $msys 'MSYS2 first start' 'true'
            }
            Say 'tools: devkitPro''s signing key and package lists'
            $key = 'BC26F752D25B92CE272E0F44F7FD5492264BB9D0'
            DkpPacman $msys 'the devkitPro key' ("pacman-key --recv-keys $key --keyserver keyserver.ubuntu.com || pacman-key --recv-keys $key --keyserver hkps://keys.openpgp.org || (curl -fsSL https://apt.devkitpro.org/devkitpro-pub.gpg -o /tmp/dkp.gpg && pacman-key --add /tmp/dkp.gpg); pacman-key --lsign-key $key")
            DkpPacman $msys 'the devkitPro keyring' 'pacman -U --noconfirm https://pkg.devkitpro.org/devkitpro-keyring.pkg.tar.zst || pacman-key --populate devkitpro'
            $conf = Join-Path $msys 'etc\pacman.conf'
            if (-not (Select-String -Path $conf -Pattern '^\[dkp-libs\]' -Quiet)) {
                [IO.File]::AppendAllText($conf, "`n[dkp-libs]`nServer = https://pkg.devkitpro.org/packages`n`n[dkp-windows]`nServer = https://pkg.devkitpro.org/packages/windows/`$arch/`n")
            }
            Say 'tools: devkitPro switch-dev, deko3d, switch-zlib, switch-libzstd (about 1 GB download)'
            DkpPacman $msys 'devkitPro packages' 'pacman -Sy --noconfirm && pacman -S --needed --noconfirm switch-dev deko3d switch-zlib switch-libzstd make && pacman -Scc --noconfirm'
            if (-not (DkpOk $localDkp)) { Fail "devkitPro is incomplete after the install: $logDir\devkitpro.log" }
        }
        $dkp = $localDkp
    }
    Set-Content -Path $dkpFile -Value $dkp
    Remove-Made $dl
    Done 'tools'
}
if (-not (Test-Path $dkpFile)) { Fail 'the tools step did not finish: run again with -From tools' }
$dkp = (Get-Content $dkpFile -TotalCount 1).Trim()
$env:DEVKITPRO = $dkp.Replace('\', '/')
$env:FFX_PYTHON = $python
$env:FFX_ZSTD = $zstd
$gcc = Join-Path $dkp 'devkitA64\bin\aarch64-none-elf-gcc.exe'
$gccVersion = (& $gcc -dumpfullversion).Trim()
Note "devkitPro $dkp (GCC $gccVersion), Python $python"
# The icon, made right away (a picture that cannot be read stops the build now, not after two hours).
$iconJpg = Join-Path $Work 'nro_icon.jpg'
if ($Icon) {
    if ((Run (Join-Path $logDir 'icon.log') $python @('-c', 'import PIL')) -ne 0) {
        RunOk 'pip install pillow' (Join-Path $logDir 'pip.log') $python @('-m', 'pip', 'install', '--disable-pip-version-check', '--no-warn-script-location', 'pillow>=11.3')
    }
    RunOk "converting the icon picture $Icon (any image format; or leave -Icon out for the default icon)" (Join-Path $logDir 'icon.log') $python @((Join-Path $repo 'tools\gh\make_icon.py'), $Icon, $iconJpg, '--mode', $IconMode)
    $m = Get-Content (Join-Path $logDir 'icon.log') | Select-String '^icon:' | Select-Object -Last 1
    if ($m) { Note $m.Line }
}
# The whole PC: every logical core for the light parallel work (shader translation, the shader cache), and as many
# compile / LTO jobs and archive scans as the memory carries (a big generated file or a 2 GB archive needs about 2 GB).
$cores = [Environment]::ProcessorCount
$ramGb = [math]::Floor((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB)
if ($Jobs -le 0) { $Jobs = [math]::Max(1, [math]::Min($cores, [math]::Floor($ramGb / 2))) }
$scanJobs = [math]::Max(1, [math]::Min($cores, [math]::Floor($ramGb / 3)))
Note "PC: $cores logical cores, $ramGb GB RAM -> $Jobs compile jobs, $scanJobs archive scans, $cores shader jobs"

# The prebuilt PC tools where the tree's scripts look for them.
$bin = Join-Path $repo 'tools\gh\bin'
$dxc = Join-Path $repo 'app\thirdparty\reblue-XenosRecomp\thirdparty\dxc-bin\bin\x64'
$places = @(
    @('rexglue.exe', 'sdk\out\win-amd64'), @('rexruntime.dll', 'sdk\out\win-amd64'),
    @('XenosRecomp.exe', 'app\out\pc\xenos_recomp\build\XenosRecomp'),
    @('spirv-cross.exe', 'out\deko3d_tools\spirv-cross-build'), @('uam.exe', 'out\deko3d_tools\uam-build'),
    @('glslangValidator.exe', 'out\glslang\StandAlone'))
foreach ($pl in $places) {
    $d = Join-Path $repo $pl[1]
    New-Item -ItemType Directory -Force $d | Out-Null
    Copy-Item -Force (Join-Path $bin $pl[0]) $d
}
Copy-Item -Force (Join-Path $dxc 'dxcompiler.dll'), (Join-Path $dxc 'dxil.dll') (Join-Path $repo 'app\out\pc\xenos_recomp\build\XenosRecomp')

# ---- 2. extract -----------------------------------------------------------------------------------------------------
$assetsSrc = Join-Path $Work 'codegen_assets'
$modules = @('default', 'z0070', 'z0130', 'z0160', 'z0161')
if (-not (IsDone 'extract')) {
    NeedFree $(if ($NoGameFiles -or $isoIsFolder) { 8 } else { 17 }) 'extracting the game and the build'
    New-Item -ItemType Directory -Force $assetsSrc, $tu | Out-Null
    if (-not $isoIsFolder -and -not $NoGameFiles) {
        Say "extract: $Iso -> $disc (about 8.7 GB: the game files for the SD card)"
        Py 'extracting the disc image' 'extract.log' @((Join-Path $repo 'tools\gh\iso_tool.py'), 'extract', $Iso, $disc)
    }
    if ($disc) {
        foreach ($m in $modules) {
            $x = Join-Path $disc "$m.xex"
            if (-not (Test-Path -LiteralPath $x)) { Fail "no $m.xex in $disc (not Final Fantasy XIII-2's disc?)" }
            Copy-Item -Force -LiteralPath $x $assetsSrc
        }
    } else {
        Say "extract: the five game modules from $Iso (the game files stay out of the SD folder: -NoGameFiles)"
        $a = @((Join-Path $repo 'tools\gh\iso_tool.py'), 'extract', $Iso, $assetsSrc, '--only') + @($modules | ForEach-Object { "$_.xex" })
        Py 'extracting the game modules' 'extract.log' $a
    }
    if (Test-Path -LiteralPath $Update -PathType Container) {
        if ($Update -ne $tu) {
            Say "extract: the title update folder $Update -> $tu"
            Copy-Item -Recurse -Force (Join-Path $Update '*') $tu
        }
    } else {
        Say "extract: the title update package $Update -> $tu"
        Py 'extracting the title update' 'extract.log' @((Join-Path $repo 'tools\stfs_extract.py'), $Update, $tu)
    }
    foreach ($m in $modules) {
        $p = Get-ChildItem -LiteralPath $tu -Recurse -Filter "$m.xexp" | Select-Object -First 1
        if ($p) { Copy-Item -Force -LiteralPath $p.FullName $assetsSrc }
    }
    $xex = Join-Path $assetsSrc 'default.xex'
    $xexp = Join-Path $assetsSrc 'default.xexp'
    if (-not (Test-Path $xexp)) { Fail "no default.xexp in the title update $Update (not title update 6?)" }
    if (-not $SkipHashCheck) {
        if ((Sha $xex) -ne $XexSha) { Fail "default.xex is not the Europe disc's (SHA-256 $(Sha $xex)). Only the EU disc + title update 6 is supported (-SkipHashCheck to try anyway)." }
        if ((Sha $xexp) -ne $XexpSha) { Fail "default.xexp is not title update 6's (SHA-256 $(Sha $xexp)); -SkipHashCheck to try anyway." }
    }
    Done 'extract'
}

# ---- 3. codegen (PowerPC -> C++, then the optimising post passes) ---------------------------------------------------
if (-not (IsDone 'codegen')) {
    NeedFree 5 'the code generation'
    Say 'codegen: default.xex + the four zone modules -> app\generated (about 20-40 minutes)'
    $assets = Join-Path $repo 'app\assets'
    if (Test-Path -LiteralPath $assets) {
        $it = Get-Item -LiteralPath $assets -Force
        if (-not ($it.Attributes -band [IO.FileAttributes]::ReparsePoint)) { Fail "app\assets exists and is not a link made by this script: move it away yourself" }
        & $cmdExe /c rmdir "$assets" | Out-Null   # the link only, never its target's files
    }
    New-Item -ItemType Junction -Path $assets -Target $assetsSrc | Out-Null
    foreach ($d in @('app\generated', 'app\generated_zones')) { Remove-Made (Join-Path $repo $d) }
    New-Item -ItemType Directory -Force (Join-Path $repo 'out') | Out-Null
    $env:REXGLUE_DUMP_IMAGE = Join-Path $repo 'out\renut_image.bin'
    $env:REX_MAX_JUMP_TABLE_ENTRIES = '1024'
    $env:REXSDK_DIR = Join-Path $repo 'sdk'
    # The post passes as this port's builds ran them (dead_ctx_defs off: it does not see the fast bodies).
    $env:BANJO_DEAD_DEFS_OFF = '1'
    $rexglue = Join-Path $repo 'sdk\out\win-amd64\rexglue.exe'
    foreach ($z in @('z0070', 'z0130', 'z0160', 'z0161')) {
        Note "codegen default.xex + $z.xex"
        RunOk "the code generator ($z)" (Join-Path $logDir "codegen_$z.log") $rexglue @('codegen', 'renut_manifest.toml', '--target', $z, '--ignore-stamp') (Join-Path $repo 'app')
    }
    Py 'the module lists' 'codegen_modules.log' @((Join-Path $repo 'tools\ffxiii2_post_codegen_modules.py'), (Join-Path $repo 'app'))
    New-Item -ItemType Directory -Force (Join-Path $repo 'app\generated\hot') | Out-Null
    Note 'the optimising post passes'
    Py 'the post passes' 'post_codegen.log' @((Join-Path $repo 'tools\post_codegen.py'))
    Done 'codegen'
}

# ---- 4. shaders -----------------------------------------------------------------------------------------------------
$sh = Join-Path $repo 'out\deko3d_shaders'
if (-not (IsDone 'shaders')) {
    NeedFree 6 'the shader translation'
    $dump = Join-Path $repo 'out\renut_shader_dump'
    $scan = Join-Path $repo 'out\shader_scan'
    foreach ($d in @($dump, $scan)) { Remove-Made $d; New-Item -ItemType Directory -Force $d | Out-Null }
    Say 'shaders: the containers in default.xex'
    Py 'the shader dump' 'shader_dump.log' @((Join-Path $repo 'tools\renut_shader_dump.py'), '--image', (Join-Path $repo 'out\renut_image.bin'), '--out', $dump)
    # The shaders the NRO carries built in (by hash; app\config\ffxiii2\embedded_shaders.txt): empty placeholders,
    # read by name only (deko3d_shader_package.py --only).
    $n = 0
    foreach ($name in Get-Content (Join-Path $repo 'app\config\ffxiii2\embedded_shaders.txt')) {
        $name = $name.Trim()
        if (-not $name) { continue }
        $f = Join-Path $dump "$name.bin"
        if (-not (Test-Path $f)) { New-Item -ItemType File $f | Out-Null; $n++ }
    }
    Note "$n placeholders for the built-in set"
    Say 'shaders: every shader container in the game files (alba_data archives, the title update, the DLC)'
    if ($isoIsFolder -or -not $NoGameFiles) {
        Py 'scanning the disc files' 'shader_scan.log' @((Join-Path $repo 'tools\ffxiii2_scan_shaders.py'), '--out', $scan, '--jobs', "$scanJobs", 'alba_data') $disc
    } else {
        Py 'scanning the disc image' 'shader_scan.log' @((Join-Path $repo 'tools\gh\iso_tool.py'), 'scan', $Iso, '--out', $scan, '--jobs', "$scanJobs")
    }
    Py 'scanning the title update' 'shader_scan_tu.log' @((Join-Path $repo 'tools\ffxiii2_scan_shaders.py'), '--out', $scan, '--jobs', "$scanJobs", (Split-Path -Leaf $tu)) (Split-Path -Parent $tu)
    if ($Dlc) {
        $dlcFiles = if (Test-Path -LiteralPath $Dlc -PathType Container) { @(Get-ChildItem -LiteralPath $Dlc -File) } else { @(Get-Item -LiteralPath $Dlc) }
        $k = 0
        foreach ($f in $dlcFiles) {
            $k++
            $x = Join-Path $Work "dlc_x\d$k"
            New-Item -ItemType Directory -Force $x | Out-Null
            $code = Run (Join-Path $logDir 'dlc_extract.log') $python @((Join-Path $repo 'tools\stfs_extract.py'), $f.FullName, $x)
            if ($code -ne 0) { Warn "$($f.Name) is not an Xbox 360 package (left out of the shader scan; still copied to the SD folder)"; continue }
            Py 'scanning the DLC' 'shader_scan_dlc.log' @((Join-Path $repo 'tools\ffxiii2_scan_shaders.py'), '--out', $scan, '--jobs', "$scanJobs", "d$k") (Join-Path $Work 'dlc_x')
        }
        Remove-Made (Join-Path $Work 'dlc_x')
    }
    Say 'shaders: XenosRecomp -> SPIR-V -> GLSL -> the deko3d packages (about 30-90 minutes)'
    $ps = Join-Path $PSHOME 'powershell.exe'
    RunOk 'the shader translation' (Join-Path $logDir 'shaders.log') $ps @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $repo 'tools\switch_shaders.ps1'), '-Scan', $scan)
    foreach ($f in @('app\out\renut_switch\deko3d_glsl_switch.cpp', 'app\out\renut_switch\shader_cache_switch.cpp', 'out\deko3d_shaders\deko3d_glsl_full.bin')) {
        if (-not (Test-Path (Join-Path $repo $f))) { Fail "the shader step did not make $f ($logDir\shaders.log)" }
    }
    Copy-Item -Force (Join-Path $sh 'deko3d_glsl_full.bin') (Join-Path $sd 'deko3d_glsl.bin')
    # What the next steps no longer need.
    foreach ($d in @('out\deko3d_input', 'out\shader_scan', 'out\deko3d_shaders\early_alpha_hlsl', 'out\deko3d_shaders\spirv', 'out\deko3d_shaders\glsl')) { Remove-Made (Join-Path $repo $d) }
    Done 'shaders'
}

# ---- 5. the shader cache (DKSH, compiled on the PC with the console's uam) -----------------------------------------
if (-not (IsDone 'cache')) {
    NeedFree 4 'the shader cache'
    Say 'cache: the pipelines of the recorded play-throughs + every generic variant (about 30-60 minutes)'
    $a = @((Join-Path $repo 'tools\deko3d_variant_cache.py'))
    foreach ($l in Get-ChildItem (Join-Path $repo 'app\config\ffxiii2\pipeline_lists') -Filter '*.bin' | Sort-Object Name) { $a += @('--list', $l.FullName) }
    $a += @('--generic', '--glsl', (Join-Path $sh 'glsl_opt'), '--out', (Join-Path $sh 'deko3d_shaders.bin'), '--jobs', "$cores")
    Py 'the shader cache' 'cache.log' $a
    if (-not (Test-Path (Join-Path $sh 'deko3d_shaders.bin'))) { Fail "the shader cache was not made ($logDir\cache.log)" }
    Copy-Item -Force (Join-Path $sh 'deko3d_shaders.bin') (Join-Path $sd 'deko3d_shaders.bin')
    Remove-Made $sh
    Done 'cache'
}

# ---- 6. the Switch build (devkitA64, link-time and profile-guided optimisation) ------------------------------------
$build = Join-Path $repo 'app\out\switch'
if (-not (IsDone 'switch')) {
    NeedFree 5 'the Switch build'
    $pgo = -not $NoPgo
    $gccMm = ($gccVersion -split '\.')[0..1] -join '.'
    if ($pgo -and $gccMm -ne $PgoGcc) { Warn "devkitA64 GCC ${gccVersion}: the PGO profile is from GCC $PgoGcc.x, so this build goes without it (slower)."; $pgo = $false }
    if ($pgo -and -not (Get-ChildItem (Join-Path $repo 'pgo') -Filter '*.gcda' -ErrorAction SilentlyContinue)) { Warn 'no pgo\*.gcda: building without the PGO profile'; $pgo = $false }
    Say "switch: the NRO (devkitA64, LTO$(if ($pgo) { ' + PGO' }); about 30-60 minutes, $Jobs jobs)"
    Remove-Made $build
    $py = $python.Replace('\', '/')
    $cfg = @('-S', (Join-Path $repo 'app'), '-B', $build, '-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$($ninja.Replace('\', '/'))",
        '-DCMAKE_BUILD_TYPE=Release', '-DKATAMARI_RENUT_HOOKS=ON',
        "-DCMAKE_TOOLCHAIN_FILE=$((Join-Path $repo 'tools\switch\cmake\switch-devkitA64.cmake').Replace('\', '/'))",
        "-DREXSDK_DIR=$((Join-Path $repo 'sdk').Replace('\', '/'))", "-DREXGLUE_SWITCH_NVK_SDK=$($env:DEVKITPRO)/portlibs/switch",
        "-DPYTHON_EXECUTABLE=$py", "-DPython3_EXECUTABLE=$py",
        '-DBANJO_GEN_R1_LOCAL=ON', '-DBANJO_GEN_LR_WRITE_SAME=ON', '-DBANJO_SWITCH_LTO=ON', "-DBANJO_SWITCH_LTO_JOBS=$Jobs")
    if ($pgo) { $cfg += '-DBANJO_PGO=use' }
    if ($Icon) { $cfg += "-DFFXIII2_NRO_ICON=$($iconJpg.Replace('\', '/'))" } else { $cfg += '-DFFXIII2_NRO_ICON=' }
    RunOk 'configuring the Switch build' (Join-Path $logDir 'switch_configure.log') $cmake $cfg
    # GCC runs the link-time optimisation's partitions in parallel only through GNU make (else: "using serial
    # compilation of N LTRANS jobs", one core for the longest part of the build). devkitPro's MSYS2 has make; its folder
    # goes at the END of PATH for the build (our CMake, Ninja and Python are called by full path). If that link fails,
    # the build is run again without it (serial, as before).
    # MSYS2 of an installed devkitPro (C:\devkitPro\msys2) or of the local one (<Tools>\msys64\opt\devkitpro -> msys64).
    # An installed devkitPro sits at a drive's root (C:\devkitPro), where the folder two levels up is empty: skip it.
    $msysRoots = @(Join-Path $dkp 'msys2')
    $up = Split-Path -Parent $dkp
    if ($up) { $up = Split-Path -Parent $up }
    if ($up) { $msysRoots += $up }
    $msysRoots = @($msysRoots | Where-Object { $_ -and (Test-Path -LiteralPath (Join-Path $_ 'usr\bin\pacman.exe')) })
    $make = $msysRoots | ForEach-Object { Join-Path $_ 'usr\bin\make.exe' } | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $make -and $msysRoots) {
        $r = @($msysRoots)[0]
        $env:MSYSTEM = 'MSYS'; $env:CHERE_INVOKING = '1'
        [void](Run (Join-Path $logDir 'devkitpro.log') (Join-Path $r 'usr\bin\bash.exe') @('-lc', 'pacman -S --needed --noconfirm make; gpgconf --kill all >/dev/null 2>&1; true') $Tools)
        Remove-Item Env:\MSYSTEM, Env:\CHERE_INVOKING -ErrorAction SilentlyContinue
        if (Test-Path (Join-Path $r 'usr\bin\make.exe')) { $make = Join-Path $r 'usr\bin\make.exe' }
    }
    $oldPath = $env:PATH
    if ($make) { $env:PATH = $env:PATH + ';' + (Split-Path -Parent $make); Note "parallel LTO with $make ($Jobs jobs)" }
    $code = Run (Join-Path $logDir 'switch_build.log') $cmake @('--build', $build, '-j', "$Jobs")
    $env:PATH = $oldPath
    if ($code -ne 0 -and $make) {
        Warn "the build stopped (exit $code); once more without parallel LTO"
        $code = Run (Join-Path $logDir 'switch_build_serial.log') $cmake @('--build', $build, '-j', "$Jobs")
    }
    if ($code -ne 0) { Get-Content (Join-Path $logDir 'switch_build*.log') -Tail 25; Fail "the Switch build failed (exit $code): $logDir\switch_build.log" }
    $nro = Join-Path $build 'renut.nro'
    if (-not (Test-Path $nro)) { Fail "the Switch build made no renut.nro ($logDir\switch_build.log)" }
    Copy-Item -Force $nro (Join-Path $sd 'ffxiii2.nro')
    if ($KeepWork) { Copy-Item -Force (Join-Path $build 'renut') (Join-Path $Out 'ffxiii2-nx.elf') -ErrorAction SilentlyContinue }
    Done 'switch'
}

# ---- 7. package -----------------------------------------------------------------------------------------------------
if (-not (IsDone 'package')) {
    Say "package: $sd"
    if (-not (Test-Path (Join-Path $sd 'ffxiii2.toml'))) { Copy-Item (Join-Path $repo 'app\ffxiii2_switch.toml') (Join-Path $sd 'ffxiii2.toml') }
    Copy-Item -Force (Join-Path $repo 'LICENSE') (Join-Path $sd 'LICENSE.txt')
    Copy-Item -Force (Join-Path $repo 'THIRD_PARTY_NOTICES.md') (Join-Path $sd 'THIRD_PARTY_NOTICES.md')
    if ($Dlc) {
        $dlcDir = Join-Path $sd 'dlc'
        New-Item -ItemType Directory -Force $dlcDir | Out-Null
        if (Test-Path -LiteralPath $Dlc -PathType Container) { Get-ChildItem -LiteralPath $Dlc -File | Copy-Item -Destination $dlcDir -Force }
        else { Copy-Item -Force -LiteralPath $Dlc $dlcDir }
        Note "DLC: $((Get-ChildItem $dlcDir -File).Count) package(s) in dlc\ (installed by the game at its next start, about 3 minutes)"
    }
    if ($isoIsFolder -and -not $NoGameFiles) {
        NeedFree 10 'copying the game files'
        Say "package: copying your disc files into the SD folder (about 8.7 GB)"
        & robocopy $Iso (Join-Path $sd 'game_root') /E /NFL /NDL /NJH /NJS /NP | Out-Null
        if ($LASTEXITCODE -ge 8) { Fail "copying the disc files failed (robocopy $LASTEXITCODE)" }
        $global:LASTEXITCODE = 0
    }
    # The title update's executable patches (default.xexp and the four z*.xexp, 1.7 MB) must ALSO sit next to the
    # disc's .xex files: the runtime loads a module's patch as its sibling "game:\<name>.xexp". Without them the game
    # runs the disc's unpatched default.xex, whose entry point the TU6 code does not have ("No function registered at
    # 82B31D08"), and it ends at once (found on the console 2026-11-11). With -NoGameFiles they go into an otherwise
    # empty game_root\ of the SD folder, so copying the folder puts them next to the disc files already on the card.
    $gameRoot = Join-Path $sd 'game_root'
    New-Item -ItemType Directory -Force $gameRoot | Out-Null
    $patches = @(Get-ChildItem -LiteralPath $tu -Recurse -File -Filter '*.xexp' | Where-Object { $_.Name -match '^(default|z\d{4})\.xexp$' })
    if (-not ($patches | Where-Object { $_.Name -eq 'default.xexp' })) { Fail "no default.xexp in $tu (the title update step): run again with -From extract" }
    foreach ($x in $patches) { Copy-Item -Force -LiteralPath $x.FullName -Destination (Join-Path $gameRoot $x.Name) }
    Note "title update patches next to the disc's executables: $(($patches | ForEach-Object Name) -join ', ')"
    Done 'package'
}

# ---- 8. copy to the SD card -------------------------------------------------------------------------------------------
if (-not (IsDone 'sdcard')) {
    if ($SdCard) {
        $dst = Join-Path $SdCard 'switch\ffxiii2-nx'
        $need = [math]::Ceiling((Get-ChildItem -LiteralPath $sd -Recurse -File | Measure-Object -Sum Length).Sum / 1GB) + 1
        $q = (Resolve-Path $SdCard).Path.Substring(0, 1)
        $free = [math]::Floor((Get-PSDrive $q).Free / 1GB)
        if ($free -lt $need) { Fail "only $free GB free on $SdCard, the SD folder needs about $need GB (copy SD\switch yourself, or free space and run again)" }
        Say "sdcard: copying the SD folder to $dst (nothing there is deleted)"
        New-Item -ItemType Directory -Force $dst | Out-Null
        $x = @()
        if (Test-Path (Join-Path $dst 'ffxiii2.toml')) { $x = @('/XF', 'ffxiii2.toml'); Note 'your ffxiii2.toml on the card is kept' }
        & robocopy $sd $dst /E /R:2 /W:2 /NFL /NDL /NJH /NJS /NP @x | Out-Null
        if ($LASTEXITCODE -ge 8) { Fail "copying to the SD card failed (robocopy $LASTEXITCODE): run again with -From sdcard" }
        $global:LASTEXITCODE = 0
    }
    Done 'sdcard'
}

# ---- 9. cleanup: everything but the SD folder (and the logs) --------------------------------------------------------
if (-not (IsDone 'cleanup')) {
    if ($KeepWork) {
        Note 'cleanup: -KeepWork, the build folders stay'
    } else {
        Say 'cleanup: the files the build made (only the SD folder and the logs stay)'
        $assets = Join-Path $repo 'app\assets'
        if (Test-Path -LiteralPath $assets) { & $cmdExe /c rmdir "$assets" | Out-Null }
        foreach ($d in @('out', 'app\out', 'app\generated', 'app\generated_zones', 'sdk\out')) { Remove-Made (Join-Path $repo $d) }
        foreach ($d in @('codegen_assets', 'tu', 'dlc_x', 'nro_icon.jpg')) { Remove-Made (Join-Path $Work $d) }
    }
    if (-not $KeepTools) {
        Note "cleanup: the downloaded tools ($Tools; -KeepTools keeps them for the next build)"
        foreach ($d in @('downloads', 'cmake', 'cmake_x', 'ninja', 'zstd', 'zstd_x', 'python', 'msys64', 'devkitpro.txt')) { Remove-Made (Join-Path $Tools $d) }
        if (-not (Get-ChildItem -LiteralPath $Tools -Force)) { Remove-Item -LiteralPath $Tools -Force -ErrorAction SilentlyContinue }
    }
    if (-not $KeepWork) {
        Remove-Made (Join-Path $Work 'steps')
        if (-not (Get-ChildItem -LiteralPath $Work -Force)) { Remove-Item -LiteralPath $Work -Force -ErrorAction SilentlyContinue }
    }
    else { Done 'cleanup' }
}

$nro = Join-Path $sd 'ffxiii2.nro'
$size = (Get-ChildItem -LiteralPath $Out -Recurse -File | Measure-Object -Sum Length).Sum
Say ("done: {0}  (NRO {1:N1} MB, SHA-256 {2}; SD folder {3:N1} GB)" -f $Out, ((Get-Item $nro).Length / 1MB), (Sha $nro), ($size / 1GB))
if ($SdCard) { Write-Host "Copied to $(Join-Path $SdCard 'switch\ffxiii2-nx'). Start the game from the Homebrew Menu" }
else { Write-Host 'Copy the "switch" folder inside it to the root of your SD card, then start the game from the Homebrew Menu' }
Write-Host 'in title-takeover mode (hold R while starting any game), not from the album.'
if ($NoGameFiles) { Write-Host 'Your game files: they must already be in sdmc:/switch/ffxiii2-nx/game_root (disc) and update (title update 6); the SD folder''s game_root adds the title update''s .xexp files next to the disc''s .xex files.' }
Write-Host "Logs: $logDir"
