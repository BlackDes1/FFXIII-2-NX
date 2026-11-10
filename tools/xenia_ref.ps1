# ffxiii2-nx (2026-11-05, the work order's Xbox 360 reference): one xenia_canary session of the original game for the
# reference catalog (docs/verification-matrix.md). xenia is secondary evidence (an emulator), not the console.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\xenia_ref.ps1 -Name ref1 [-Seconds 900] [-Every 15]
#     [-Iso <path>]
# - Starts Port\xenia_canary.exe on the disc image (default Port\Final Fantasy XIII-2 (Europe).iso) with two key overrides
#   on its command line (A = Enter, B = N, D-pad = arrow keys, right stick = numpad 8/2/4/6; the user's config binds A/B to
#   German-layout keys and the arrows to the right stick): the config file itself is not edited by this script.
#   --keyboard_mode=1: the user's config has 0 (the keyboard does not drive the pad). Start = X, Back = Z, LB = 1, RB = 3, X = L, Y = P, left stick = W/A/S/D (the user's config).
# - Captures the xenia window every -Every seconds into out\xenia_ref\<Name>\ref_NNN_TTTs.png (the window's client area
#   as it is on the screen: keep it visible), with the time since the start.
# - One session at a time: it does not start while any xenia_canary runs; it stops only the process it started (by id).
param([Parameter(Mandatory)][string]$Name, [int]$Seconds = 900, [int]$Every = 15, [string]$Iso = '')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$works = Split-Path -Parent $repo
$port = Split-Path -Parent $works
$xenia = Join-Path $port 'xenia_canary.exe'
if (-not $Iso) { $Iso = Join-Path $port 'Final Fantasy XIII-2 (Europe).iso' }
$out = Join-Path $repo "out\xenia_ref\$Name"
New-Item -ItemType Directory -Force $out | Out-Null
if (-not (Test-Path $xenia)) { throw "no xenia at $xenia" }
if (-not (Test-Path $Iso)) { throw "no disc image at $Iso" }
$running = Get-Process xenia_canary -ErrorAction SilentlyContinue
if ($running) { "a xenia_canary is running (ids $($running.Id -join ', ')): not started"; exit 2 }

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class XRefWin {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
[void][XRefWin]::SetProcessDPIAware()

$t0 = Get-Date
$p = Start-Process -FilePath $xenia -ArgumentList @(('"' + $Iso + '"'), '--keyboard_mode=1', '--keybind_a=0x0D', '--keybind_b=N', '--keybind_dpad_up=0x26', '--keybind_dpad_down=0x28', '--keybind_dpad_left=0x25', '--keybind_dpad_right=0x27', '--keybind_right_thumb_up=0x68', '--keybind_right_thumb_down=0x62', '--keybind_right_thumb_left=0x64', '--keybind_right_thumb_right=0x66') `
     -WorkingDirectory $port -PassThru
"started xenia pid $($p.Id) at $t0 on $Iso" | Tee-Object -FilePath (Join-Path $out 'run.txt')
$n = 0
$next = $t0.AddSeconds($Every)
while (((Get-Date) - $t0).TotalSeconds -lt $Seconds -and -not $p.HasExited) {
  Start-Sleep -Milliseconds 500
  if ((Get-Date) -lt $next) { continue }
  $next = $next.AddSeconds($Every)
  $p.Refresh()
  $h = $p.MainWindowHandle
  if ($h -eq [IntPtr]::Zero) { continue }
  $r = New-Object XRefWin+RECT
  if (-not [XRefWin]::GetClientRect($h, [ref]$r)) { continue }
  $pt = New-Object XRefWin+POINT
  if (-not [XRefWin]::ClientToScreen($h, [ref]$pt)) { continue }
  $w = $r.Right - $r.Left; $hgt = $r.Bottom - $r.Top
  if ($w -lt 64 -or $hgt -lt 64) { continue }
  $n++
  $secs = [int]((Get-Date) - $t0).TotalSeconds
  $bmp = New-Object System.Drawing.Bitmap $w, $hgt
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($pt.X, $pt.Y, 0, 0, $bmp.Size)
  $file = Join-Path $out ('ref_{0:D3}_{1:D4}s.png' -f $n, $secs)
  $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
}
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; "stopped xenia pid $($p.Id) after $Seconds s" }
else { "xenia exited by itself after $([int]((Get-Date) - $t0).TotalSeconds) s" }
"end $(Get-Date), $n captures" | Add-Content (Join-Path $out 'run.txt')
$log = Join-Path $port 'xenia.log'
if (Test-Path $log) { Copy-Item $log (Join-Path $out 'xenia.log') }
