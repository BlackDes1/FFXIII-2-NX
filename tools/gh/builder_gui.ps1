# ffxiii2-nx: the builder window (FFXIII-2-NX Builder.bat). Pick your game files, an icon and the SD card, press Build:
# it runs tools\gh\build_switch.ps1 (downloads the tools, builds the Switch version from your files, copies it to the
# SD card) and shows its progress and log. Nothing here changes the build itself: the same script runs from the command
# line (build_switch.cmd).
#
# Test options (all optional): -Iso -Update -Dlc -Icon -Out -SdCard fill the fields; -AutoStart presses Build;
# -CloseWhenDone closes the window when the build ends (exit code = the build's); -Screenshot <png> saves a picture of
# the window and closes it; -ExtraArgs passes more switches to build_switch.ps1 (e.g. 'KeepWork FreshTools').
param(
    [string]$Iso = '', [string]$Update = '', [string]$Dlc = '', [string]$Icon = '', [string]$Out = '',
    [string]$SdCard = '', [string]$ExtraArgs = '', [switch]$NoGameFiles, [switch]$AutoStart, [switch]$CloseWhenDone,
    [string]$Screenshot = ''
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$buildScript = Join-Path $PSScriptRoot 'build_switch.ps1'
$settingsFile = Join-Path $repo 'builder_settings.json'

# The build's output, read on background threads into a queue the window empties with a timer.
if (-not ('FfxBuildProcess' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Text;
public class FfxBuildProcess {
    public ConcurrentQueue<string> Lines = new ConcurrentQueue<string>();
    public Process P;
    public void Start(string exe, string args, string dir) {
        var si = new ProcessStartInfo(exe, args);
        si.WorkingDirectory = dir;
        si.UseShellExecute = false;
        si.CreateNoWindow = true;
        si.RedirectStandardOutput = true;
        si.RedirectStandardError = true;
        var enc = Encoding.GetEncoding(System.Globalization.CultureInfo.CurrentCulture.TextInfo.OEMCodePage);
        si.StandardOutputEncoding = enc;
        si.StandardErrorEncoding = enc;
        P = new Process();
        P.StartInfo = si;
        P.EnableRaisingEvents = true;
        P.OutputDataReceived += (s, e) => { if (e.Data != null) Lines.Enqueue(e.Data); };
        P.ErrorDataReceived += (s, e) => { if (e.Data != null) Lines.Enqueue(e.Data); };
        P.Start();
        P.BeginOutputReadLine();
        P.BeginErrorReadLine();
    }
    public bool Running { get { return P != null && !P.HasExited; } }
    // WaitForExit() would also wait for the end of the output, which a program the build left running (gpg-agent, say)
    // can keep open: the window would hang. The timeout form waits for the process only.
    public int Code { get { P.WaitForExit(5000); return P.HasExited ? P.ExitCode : -1; } }
    public void Kill() {
        if (!Running) return;
        var k = Process.Start(new ProcessStartInfo("taskkill", "/PID " + P.Id + " /T /F") { CreateNoWindow = true, UseShellExecute = false });
        k.WaitForExit();
    }
}
'@
}

# ---- settings of the last run --------------------------------------------------------------------------------------
$saved = @{}
try { if (Test-Path $settingsFile) { (Get-Content $settingsFile -Raw | ConvertFrom-Json).PSObject.Properties | ForEach-Object { $saved[$_.Name] = $_.Value } } } catch { }
function Pick([string]$given, [string]$key, [string]$default = '') {
    if ($given) { return $given }
    if ($saved.ContainsKey($key) -and $saved[$key]) { return [string]$saved[$key] }
    return $default
}

# ---- the window -----------------------------------------------------------------------------------------------------
$font = New-Object System.Drawing.Font('Segoe UI', 9)
$form = New-Object System.Windows.Forms.Form
$form.Text = 'FFXIII-2 NX Builder'
$form.Font = $font
$form.AutoScaleMode = [System.Windows.Forms.AutoScaleMode]::Font
$form.ClientSize = New-Object System.Drawing.Size(760, 740)
$form.MinimumSize = New-Object System.Drawing.Size(700, 640)
$form.StartPosition = 'CenterScreen'
$form.BackColor = [System.Drawing.Color]::FromArgb(250, 250, 252)

$title = New-Object System.Windows.Forms.Label
$title.Text = 'Final Fantasy XIII-2 for the Nintendo Switch'
$title.Font = New-Object System.Drawing.Font('Segoe UI Semibold', 14)
$title.AutoSize = $true
$title.Location = New-Object System.Drawing.Point(16, 12)
$form.Controls.Add($title)
$sub = New-Object System.Windows.Forms.Label
$sub.Text = 'Builds the Switch version from your own game files: the tools are downloaded, the game is compiled on this PC, and the result goes to your SD card. No game data comes with this builder.'
$sub.Location = New-Object System.Drawing.Point(18, 44)
$sub.Size = New-Object System.Drawing.Size(724, 34)
$sub.Anchor = 'Top, Left, Right'
$sub.ForeColor = [System.Drawing.Color]::FromArgb(80, 80, 90)
$form.Controls.Add($sub)

$tip = New-Object System.Windows.Forms.ToolTip
$y = 84
function Add-Group([string]$text, [int]$height) {
    $g = New-Object System.Windows.Forms.GroupBox
    $g.Text = $text
    $g.Location = New-Object System.Drawing.Point(12, $script:y)
    $g.Size = New-Object System.Drawing.Size(736, $height)
    $g.Anchor = 'Top, Left, Right'
    $form.Controls.Add($g)
    $script:y += $height + 6
    return $g
}
# A row: label, text box, and buttons ('file:<filter>' / 'folder').
function Add-PathRow($group, [int]$row, [string]$label, [string]$value, [string[]]$buttons, [string]$hint) {
    $top = 22 + 30 * $row
    $l = New-Object System.Windows.Forms.Label
    $l.Text = $label
    $l.Location = New-Object System.Drawing.Point(10, ($top + 4))
    $l.Size = New-Object System.Drawing.Size(150, 20)
    $group.Controls.Add($l)
    $t = New-Object System.Windows.Forms.TextBox
    $t.Text = $value
    $t.Location = New-Object System.Drawing.Point(160, $top)
    $right = 10 + 76 * $buttons.Count
    $t.Size = New-Object System.Drawing.Size((736 - 160 - $right - 6), 24)
    $t.Anchor = 'Top, Left, Right'
    $group.Controls.Add($t)
    if ($hint) { $tip.SetToolTip($t, $hint); $tip.SetToolTip($l, $hint) }
    $x = 736 - $right
    foreach ($b in $buttons) {
        $btn = New-Object System.Windows.Forms.Button
        $btn.Location = New-Object System.Drawing.Point($x, ($top - 1))
        $btn.Size = New-Object System.Drawing.Size(72, 26)
        $btn.Anchor = 'Top, Right'
        $box = $t
        if ($b -like 'file:*') {
            $btn.Text = 'File...'
            $filter = $b.Substring(5)
            $btn.Add_Click({
                $d = New-Object System.Windows.Forms.OpenFileDialog
                $d.Filter = $filter
                if ($box.Text -and (Test-Path -LiteralPath $box.Text)) { $d.InitialDirectory = Split-Path -Parent $box.Text }
                if ($d.ShowDialog($form) -eq 'OK') { $box.Text = $d.FileName }
            }.GetNewClosure())
        } else {
            $btn.Text = 'Folder...'
            $btn.Add_Click({
                $d = New-Object System.Windows.Forms.FolderBrowserDialog
                if ($box.Text -and (Test-Path -LiteralPath $box.Text -PathType Container)) { $d.SelectedPath = $box.Text }
                if ($d.ShowDialog($form) -eq 'OK') { $box.Text = $d.SelectedPath }
            }.GetNewClosure())
        }
        $group.Controls.Add($btn)
        $x += 76
    }
    return $t
}

$gGame = Add-Group 'Your game files' 116
$tIso = Add-PathRow $gGame 0 'Disc image (required)' (Pick $Iso 'Iso') @('file:Disc image (*.iso)|*.iso|All files (*.*)|*.*', 'folder') 'Final Fantasy XIII-2 (Europe), a plain .iso, or a folder with the disc''s files'
$tUpd = Add-PathRow $gGame 1 'Title update 6 (required)' (Pick $Update 'Update') @('file:Title update (tu*)|tu*|All files (*.*)|*.*', 'folder') 'The package tu0000000b_00000000, or a folder you extracted it into'
$tDlc = Add-PathRow $gGame 2 'DLC (optional)' (Pick $Dlc 'Dlc') @('file:All files (*.*)|*.*', 'folder') 'The game''s Xbox 360 DLC package(s): one file or a folder of them. Leave empty to build without DLC.'

$gIcon = Add-Group 'Icon in the Homebrew Menu (optional)' 112
$defaultIcon = ''
$imgExt = 'png|jpe?g|bmp|gif|tiff?|webp|ico|tga|dds|psd|avif|heic|heif|jxl|qoi|pcx|ppm|svg'
$c = Get-ChildItem -LiteralPath $repo -File -Filter 'icon.*' -ErrorAction SilentlyContinue | Where-Object { $_.Extension -match "^\.($imgExt)$" } | Select-Object -First 1
if ($c) { $defaultIcon = $c.FullName }
$picFilter = 'file:Pictures|*.png;*.jpg;*.jpeg;*.jfif;*.bmp;*.gif;*.tif;*.tiff;*.webp;*.ico;*.tga;*.dds;*.psd;*.avif;*.heic;*.heif;*.jxl;*.qoi;*.pcx;*.ppm;*.svg|All files (*.*)|*.*'
$tIcon = Add-PathRow $gIcon 0 'Picture' (Pick $Icon 'Icon' $defaultIcon) @($picFilter) 'Any picture in any size and format (PNG, JPG, WebP, BMP, GIF, ICO, TIFF, PSD, HEIC, ...): the build makes it the 256x256 JPEG icon of the .nro. Empty: libnx''s default icon.'
$tIcon.Width -= 96
$pic = New-Object System.Windows.Forms.PictureBox
$pic.Size = New-Object System.Drawing.Size(80, 80)
$pic.Location = New-Object System.Drawing.Point(642, 20)
$pic.Anchor = 'Top, Right'
$pic.SizeMode = 'Zoom'
$pic.BackColor = [System.Drawing.Color]::Black
$gIcon.Controls.Add($pic)
foreach ($c in $gIcon.Controls) { if ($c -is [System.Windows.Forms.Button]) { $c.Left -= 96 } }
$iconNote = New-Object System.Windows.Forms.Label
$iconNote.Text = 'Any size and format: converted to the 256x256 icon. Only used for your build, never uploaded.'
$iconNote.ForeColor = [System.Drawing.Color]::FromArgb(100, 100, 110)
$iconNote.Location = New-Object System.Drawing.Point(160, 54)
$iconNote.Size = New-Object System.Drawing.Size(470, 20)
$gIcon.Controls.Add($iconNote)
$lMode = New-Object System.Windows.Forms.Label
$lMode.Text = 'Shape'
$lMode.Location = New-Object System.Drawing.Point(10, 82)
$lMode.Size = New-Object System.Drawing.Size(150, 20)
$gIcon.Controls.Add($lMode)
$cmbMode = New-Object System.Windows.Forms.ComboBox
$cmbMode.DropDownStyle = 'DropDownList'
$cmbMode.Location = New-Object System.Drawing.Point(160, 79)
$cmbMode.Size = New-Object System.Drawing.Size(300, 24)
[void]$cmbMode.Items.AddRange(@('Fit: the whole picture (black bars if not square)', 'Fill: cropped to a square', 'Stretch: squeezed to a square'))
$modes = @('fit', 'fill', 'stretch')
$cmbMode.SelectedIndex = [math]::Max(0, [array]::IndexOf($modes, [string](Pick '' 'IconMode' 'fit')))
$gIcon.Controls.Add($cmbMode)
$gIcon.Height += 8
$script:y += 8

# The picture as the build will make it (256x256, the chosen shape, on black). System.Drawing reads PNG, JPEG, BMP,
# GIF, TIFF, ICO; other formats through Windows' own codecs (WIC: WebP, HEIC, AVIF, JPEG XL when installed). A format
# neither reads still works for the build (tools\gh\make_icon.py, Pillow): only the preview is missing then.
function Read-Picture([string]$path) {
    $bytes = [IO.File]::ReadAllBytes($path)   # no file lock
    try { return [System.Drawing.Image]::FromStream((New-Object IO.MemoryStream(, $bytes))) } catch { }
    try {
        Add-Type -AssemblyName PresentationCore, WindowsBase
        $dec = [System.Windows.Media.Imaging.BitmapDecoder]::Create((New-Object IO.MemoryStream(, $bytes)),
            [System.Windows.Media.Imaging.BitmapCreateOptions]::None, [System.Windows.Media.Imaging.BitmapCacheOption]::OnLoad)
        $enc = New-Object System.Windows.Media.Imaging.PngBitmapEncoder
        $enc.Frames.Add($dec.Frames[0])
        $ms = New-Object IO.MemoryStream
        $enc.Save($ms)
        $ms.Position = 0
        return [System.Drawing.Image]::FromStream($ms)
    } catch { }
    return $null
}
function Show-Icon {
    if ($pic.Image) { $pic.Image.Dispose(); $pic.Image = $null }
    $tip.SetToolTip($pic, '')
    if (-not ($tIcon.Text -and (Test-Path -LiteralPath $tIcon.Text -PathType Leaf))) { return }
    $img = Read-Picture $tIcon.Text
    if (-not $img) { $tip.SetToolTip($pic, 'No preview for this format: the build still converts it.'); return }
    try {
        $bmp = New-Object System.Drawing.Bitmap 256, 256
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.Clear([System.Drawing.Color]::Black)
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $w = [double]$img.Width; $h = [double]$img.Height
        switch ($modes[[math]::Max(0, $cmbMode.SelectedIndex)]) {
            'stretch' { $g.DrawImage($img, 0, 0, 256, 256) }
            'fill' { $k = [math]::Max(256 / $w, 256 / $h); $g.DrawImage($img, [int]((256 - $w * $k) / 2), [int]((256 - $h * $k) / 2), [int]($w * $k), [int]($h * $k)) }
            default { $k = [math]::Min(256 / $w, 256 / $h); $g.DrawImage($img, [int]((256 - $w * $k) / 2), [int]((256 - $h * $k) / 2), [int]($w * $k), [int]($h * $k)) }
        }
        $g.Dispose()
        $pic.Image = $bmp
        $tip.SetToolTip($pic, ('{0} x {1} -> 256 x 256 JPEG' -f $img.Width, $img.Height))
    } finally { $img.Dispose() }
}
$tIcon.Add_TextChanged({ Show-Icon })
$cmbMode.Add_SelectedIndexChanged({ Show-Icon })
Show-Icon

$gOut = Add-Group 'Output' 140
$tOut = Add-PathRow $gOut 0 'SD folder on this PC' (Pick $Out 'Out' (Join-Path $repo 'SD')) @('folder') 'Where the finished switch\ffxiii2-nx folder is made'
$cbCard = New-Object System.Windows.Forms.CheckBox
$cbCard.Text = 'Copy to the SD card:'
$cbCard.Location = New-Object System.Drawing.Point(10, 54)
$cbCard.Size = New-Object System.Drawing.Size(150, 22)
$gOut.Controls.Add($cbCard)
$cmbCard = New-Object System.Windows.Forms.ComboBox
$cmbCard.Location = New-Object System.Drawing.Point(160, 53)
$cmbCard.Size = New-Object System.Drawing.Size(340, 24)
$cmbCard.DropDownStyle = 'DropDownList'
$gOut.Controls.Add($cmbCard)
$btnRefresh = New-Object System.Windows.Forms.Button
$btnRefresh.Text = 'Refresh'
$btnRefresh.Location = New-Object System.Drawing.Point(506, 52)
$btnRefresh.Size = New-Object System.Drawing.Size(72, 26)
$gOut.Controls.Add($btnRefresh)
function Fill-Cards {
    $cmbCard.Items.Clear()
    foreach ($d in [System.IO.DriveInfo]::GetDrives()) {
        if ($d.DriveType -eq 'Removable' -and $d.IsReady) {
            $label = if ($d.VolumeLabel) { $d.VolumeLabel } else { 'removable' }
            [void]$cmbCard.Items.Add(('{0}  ({1}, {2:N0} GB free)' -f $d.Name, $label, ($d.AvailableFreeSpace / 1GB)))
        }
    }
    if ($SdCard) { [void]$cmbCard.Items.Add($SdCard) }
    if ($cmbCard.Items.Count) { $cmbCard.SelectedIndex = $cmbCard.Items.Count - 1 }
    $cbCard.Enabled = [bool]$cmbCard.Items.Count
    if (-not $cmbCard.Items.Count) { $cbCard.Checked = $false; $cbCard.Text = 'Copy to the SD card' ; $tip.SetToolTip($cbCard, 'No SD card found: insert it and press Refresh') }
    else { $cbCard.Text = 'Copy to the SD card:' }
}
$btnRefresh.Add_Click({ Fill-Cards })
Fill-Cards
if ($SdCard) { $cbCard.Checked = $true }
$cbGame = New-Object System.Windows.Forms.CheckBox
$cbGame.Text = 'Put the game files (8.7 GB) into the SD folder (untick when they are already on your SD card)'
$cbGame.Location = New-Object System.Drawing.Point(10, 82)
$cbGame.Size = New-Object System.Drawing.Size(700, 22)
$cbGame.Checked = -not $NoGameFiles -and -not ($saved.ContainsKey('NoGameFiles') -and $saved['NoGameFiles'])
$gOut.Controls.Add($cbGame)
$cbTools = New-Object System.Windows.Forms.CheckBox
$cbTools.Text = 'Keep the downloaded tools (faster next build, about 2.5 GB on the disk)'
$cbTools.Location = New-Object System.Drawing.Point(10, 108)
$cbTools.Size = New-Object System.Drawing.Size(560, 22)
$tip.SetToolTip($cbTools, 'Faster next build, about 2.5 GB more on the disk')
$gOut.Controls.Add($cbTools)

# Buttons and progress.
$btnBuild = New-Object System.Windows.Forms.Button
$btnBuild.Text = 'Build'
$btnBuild.Font = New-Object System.Drawing.Font('Segoe UI Semibold', 10)
$btnBuild.Location = New-Object System.Drawing.Point(12, $y)
$btnBuild.Size = New-Object System.Drawing.Size(120, 34)
$btnBuild.BackColor = [System.Drawing.Color]::FromArgb(36, 99, 196)
$btnBuild.ForeColor = [System.Drawing.Color]::White
$btnBuild.FlatStyle = 'Flat'
$form.Controls.Add($btnBuild)
$btnCancel = New-Object System.Windows.Forms.Button
$btnCancel.Text = 'Stop'
$btnCancel.Location = New-Object System.Drawing.Point(140, $y)
$btnCancel.Size = New-Object System.Drawing.Size(80, 34)
$btnCancel.Enabled = $false
$form.Controls.Add($btnCancel)
$btnOpen = New-Object System.Windows.Forms.Button
$btnOpen.Text = 'Open SD folder'
$btnOpen.Location = New-Object System.Drawing.Point(228, $y)
$btnOpen.Size = New-Object System.Drawing.Size(110, 34)
$form.Controls.Add($btnOpen)
$status = New-Object System.Windows.Forms.Label
$status.Text = 'Ready.'
$status.Location = New-Object System.Drawing.Point(350, ($y + 8))
$status.Size = New-Object System.Drawing.Size(398, 20)
$status.Anchor = 'Top, Left, Right'
$form.Controls.Add($status)
$y += 42
$bar = New-Object System.Windows.Forms.ProgressBar
$bar.Location = New-Object System.Drawing.Point(12, $y)
$bar.Size = New-Object System.Drawing.Size(736, 18)
$bar.Anchor = 'Top, Left, Right'
$steps = @('tools', 'extract', 'codegen', 'shaders', 'cache', 'switch', 'package', 'sdcard', 'cleanup')
$bar.Maximum = $steps.Count
$form.Controls.Add($bar)
$y += 26
$log = New-Object System.Windows.Forms.TextBox
$log.Multiline = $true
$log.ScrollBars = 'Vertical'
$log.ReadOnly = $true
$log.WordWrap = $false
$log.Font = New-Object System.Drawing.Font('Consolas', 9)
$log.BackColor = [System.Drawing.Color]::FromArgb(24, 24, 28)
$log.ForeColor = [System.Drawing.Color]::FromArgb(220, 220, 225)
$log.Location = New-Object System.Drawing.Point(12, $y)
$log.Size = New-Object System.Drawing.Size(736, (740 - $y - 12))
$log.Anchor = 'Top, Bottom, Left, Right'
$form.Controls.Add($log)

$stepNames = @{ tools = 'Downloading and setting up the tools'; extract = 'Reading your game files'; codegen = 'Compiling the game code to C++ (20-40 min)';
    shaders = 'Translating the shaders (30-90 min)'; cache = 'Building the shader cache (30-60 min)'; switch = 'Compiling the Switch version (30-60 min)';
    package = 'Making the SD folder'; sdcard = 'Copying to the SD card'; cleanup = 'Cleaning up' }
$script:job = $null
$script:started = $null
$script:exitCode = $null
function Append([string]$text) {
    $log.AppendText($text + [Environment]::NewLine)
}
function Set-Busy([bool]$busy) {
    foreach ($g in @($gGame, $gIcon, $gOut)) { $g.Enabled = -not $busy }
    $btnBuild.Enabled = -not $busy
    $btnCancel.Enabled = $busy
}
function Save-Settings {
    try {
        @{ Iso = $tIso.Text; Update = $tUpd.Text; Dlc = $tDlc.Text; Icon = $tIcon.Text; IconMode = $modes[[math]::Max(0, $cmbMode.SelectedIndex)]; Out = $tOut.Text; NoGameFiles = -not $cbGame.Checked } |
            ConvertTo-Json | Set-Content -Path $settingsFile -Encoding UTF8
    } catch { }
}
function Quote([string]$a) { if ($a -match '[\s"]') { return '"' + ($a -replace '"', '\"').TrimEnd('\') + '"' } return $a }

$btnBuild.Add_Click({
    $iso = $tIso.Text.Trim().Trim('"'); $upd = $tUpd.Text.Trim().Trim('"'); $dlc = $tDlc.Text.Trim().Trim('"')
    $ico = $tIcon.Text.Trim().Trim('"'); $outDir = $tOut.Text.Trim().Trim('"')
    if (-not $iso -or -not (Test-Path -LiteralPath $iso)) { [void][System.Windows.Forms.MessageBox]::Show($form, 'Choose your disc image (.iso) of Final Fantasy XIII-2 (Europe), or the folder with its files.', 'Disc image missing'); return }
    if (-not $upd -or -not (Test-Path -LiteralPath $upd)) { [void][System.Windows.Forms.MessageBox]::Show($form, 'Choose title update 6 (the file tu0000000b_00000000, or its folder).', 'Title update missing'); return }
    if ($dlc -and -not (Test-Path -LiteralPath $dlc)) { [void][System.Windows.Forms.MessageBox]::Show($form, "No DLC at $dlc (leave the field empty to build without DLC).", 'DLC'); return }
    if ($ico -and -not (Test-Path -LiteralPath $ico -PathType Leaf)) { [void][System.Windows.Forms.MessageBox]::Show($form, "No picture at $ico (leave the field empty for the default icon).", 'Icon'); return }
    $a = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $buildScript, '-Iso', $iso, '-Update', $upd)
    if ($dlc) { $a += @('-Dlc', $dlc) }
    if ($ico) { $a += @('-Icon', $ico, '-IconMode', $modes[[math]::Max(0, $cmbMode.SelectedIndex)]) }
    if ($outDir) { $a += @('-Out', $outDir) }
    if ($cbCard.Checked -and $cmbCard.SelectedItem) { $a += @('-SdCard', (([string]$cmbCard.SelectedItem) -split '  \(')[0]) }
    if (-not $cbGame.Checked) { $a += '-NoGameFiles' }
    if ($cbTools.Checked) { $a += '-KeepTools' }
    $argLine = ($a | ForEach-Object { Quote $_ }) -join ' '
    # -ExtraArgs 'KeepWork FreshTools': more switches of build_switch.ps1 (names with or without the dash).
    foreach ($x in ($ExtraArgs -split '[ ,;]+' | Where-Object { $_ })) { $argLine += ' -' + $x.TrimStart('-') }
    Save-Settings
    $log.Clear()
    Append "build_switch.ps1 $($argLine.Substring($argLine.IndexOf('-Iso')))"
    $bar.Value = 0
    $status.Text = 'Starting...'
    Set-Busy $true
    $script:started = Get-Date
    $script:exitCode = $null
    $script:job = New-Object FfxBuildProcess
    $ps = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $script:job.Start($ps, $argLine, $repo)
    $timer.Start()
})
$btnCancel.Add_Click({
    if ($script:job -and $script:job.Running) {
        if ([System.Windows.Forms.MessageBox]::Show($form, 'Stop the build? Press Build again later: it continues where it stopped.', 'Stop', 'YesNo') -eq 'Yes') {
            $script:job.Kill()
            Append '== stopped by you'
        }
    }
})
$btnOpen.Add_Click({
    $p = $tOut.Text.Trim().Trim('"')
    if ($p -and (Test-Path -LiteralPath $p)) { Start-Process explorer.exe $p } else { [void][System.Windows.Forms.MessageBox]::Show($form, 'The SD folder is made by the build.', 'SD folder') }
})

$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 200
$timer.Add_Tick({
    $line = $null
    $n = 0
    $sb = New-Object System.Text.StringBuilder
    while ($n -lt 500 -and $script:job.Lines.TryDequeue([ref]$line)) {
        [void]$sb.AppendLine($line)
        if ($line -match '^== (\w+):') {
            $k = [array]::IndexOf($steps, $Matches[1])
            if ($k -ge 0) { $bar.Value = $k; $status.Text = "$($k + 1)/$($steps.Count)  $($stepNames[$steps[$k]])" }
        }
        $n++
    }
    if ($sb.Length) { $log.AppendText($sb.ToString()) }
    if ($script:started) {
        $m = [int]((Get-Date) - $script:started).TotalMinutes
        $form.Text = "FFXIII-2 NX Builder - $m min"
    }
    if (-not $script:job.Running -and $script:job.Lines.IsEmpty) {
        $timer.Stop()
        $code = $script:job.Code
        $script:exitCode = $code
        Set-Busy $false
        $form.Text = 'FFXIII-2 NX Builder'
        try {
            New-Item -ItemType Directory -Force (Join-Path $repo 'logs') | Out-Null
            Set-Content -Path (Join-Path $repo ('logs\builder_gui_{0}.log' -f (Get-Date -Format 'yyyyMMdd_HHmmss'))) -Value $log.Text -Encoding UTF8
        } catch { }
        if ($code -eq 0) {
            $bar.Value = $bar.Maximum
            $status.Text = 'Done. Start the game from the Homebrew Menu (hold R on a game).'
            Append '== finished'
            if (-not $CloseWhenDone) { [void][System.Windows.Forms.MessageBox]::Show($form, 'The build is finished. The SD folder is ready' + $(if ($cbCard.Checked) { ' and copied to your SD card.' } else { ': copy its "switch" folder to the root of your SD card.' }), 'FFXIII-2 NX Builder') }
        } else {
            $status.Text = "Stopped (exit $code). Press Build to continue where it stopped."
            Append "== the build stopped (exit $code); the log files are in $repo\logs"
        }
        if ($CloseWhenDone) { $form.Close() }
    }
})

$form.Add_FormClosing({
    param($s, $e)
    if ($script:job -and $script:job.Running -and -not $CloseWhenDone) {
        if ([System.Windows.Forms.MessageBox]::Show($form, 'A build is running. Stop it and close?', 'Close', 'YesNo') -ne 'Yes') { $e.Cancel = $true; return }
        $script:job.Kill()
    }
})
$form.Add_Shown({
    $form.Activate()
    if ($Screenshot) {
        $form.Refresh()
        $bmp = New-Object System.Drawing.Bitmap($form.Width, $form.Height)
        $form.DrawToBitmap($bmp, (New-Object System.Drawing.Rectangle(0, 0, $form.Width, $form.Height)))
        $bmp.Save($Screenshot, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        $form.Close()
        return
    }
    if ($AutoStart) { $btnBuild.PerformClick() }
})
[void]$form.ShowDialog()
if ($null -ne $script:exitCode) { exit $script:exitCode }
exit 0
