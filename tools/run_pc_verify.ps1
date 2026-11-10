# katamari-nx: a PC verify run on Renut: every CPU enhancement checks itself on every use (banjo_cpu_verify), the guard on
# every call of every hook, the transform chain's own check on every call (docs/optimising-functions.md, "Checking"). For
# a new game, move the save out of out/pc_user first (it changes the menu path; run_pc.ps1's --user_data_root comes first
# and wins over a second one).
#   powershell -File tools/run_pc_verify.ps1 [-Seconds 300] [-Name verify1] [-Every 15] [-Script "20:START,24:A"] [-Sticks 240]
param([int]$Seconds = 300, [string]$Name = "verify1", [int]$Every = 15, [string]$Script = "", [int]$Sticks = 0)
$root = Split-Path -Parent $PSScriptRoot
$extra = "--banjo_cpu_verify=true --banjo_hot_guard_calls=4000 --banjo_hot_guard_period=1 " +
         "--katamari_hand_transform_chain_verify=true --katamari_hand_sphere_box=true --input_script=$Script"
if ($Sticks -gt 0) { $extra += " --input_script_sticks=$Sticks" }
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "tools\run_pc.ps1") -Seconds $Seconds -Name $Name -Every $Every -Visible -Extra $extra
