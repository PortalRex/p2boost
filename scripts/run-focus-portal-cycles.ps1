param(
    [string]$GameRoot = 'D:\SteamLibrary\steamapps\common\Portal 2',
    [string]$Output = 'artifacts\benchmarks\focus-portal.csv',
    [string]$RconPassword = 'p2boostlab',
    [int]$Cycles = 30,
    [ValidateSet('AltTab','Minimize')][string]$FocusMode = 'AltTab',
    [switch]$TraceP2Boost
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$presentMon = Join-Path $repo '.tools\downloads\PresentMon.exe'
$outputPath = [IO.Path]::GetFullPath((Join-Path $repo $Output))
$eventPath = [IO.Path]::ChangeExtension($outputPath, '.events.csv')
New-Item -ItemType Directory -Force (Split-Path $outputPath) | Out-Null
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class P2BoostFocusCycle {
    [DllImport("user32.dll")] public static extern bool ShowWindowAsync(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
}
'@
$focusSink = $null
if ($FocusMode -eq 'AltTab') {
    Add-Type -AssemblyName System.Windows.Forms
    $focusSink = [Windows.Forms.Form]::new()
    $focusSink.Text = 'p2boost focus-cycle target'
    $focusSink.ShowInTaskbar = $false
    $focusSink.Size = [Drawing.Size]::new(160, 80)
    $focusSink.Show()
}
function Get-GameProcess {
    $process = Get-Process portal2 -ErrorAction Stop |
        Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } | Select-Object -First 1
    if (-not $process) { throw 'Portal 2 has no top-level window.' }
    return $process
}
$gameProcess = Get-GameProcess
$listener = Get-NetTCPConnection -OwningProcess $gameProcess.Id -State Listen -ErrorAction Stop |
    Where-Object { $_.LocalPort -eq 27015 } | Select-Object -First 1
if (-not $listener) { throw 'Launch Portal 2 with -usercon and a non-empty rcon_password.' }
if ($TraceP2Boost) {
    & (Join-Path $PSScriptRoot 'invoke-rcon.ps1') -ProcessId $gameProcess.Id `
        -Password $RconPassword -Command @('p2boost_trace 1') | Out-Null
}

$duration = [Math]::Max(30, [int]($Cycles * 1.5 + 8))
$arguments = @(
    '--process_name', 'portal2.exe', '--output_file', $outputPath, '--v2_metrics',
    '--qpc_time', '--timed', $duration, '--terminate_after_timed',
    '--no_console_stats', '--set_circular_buffer_size', '32768'
)
$capture = Start-Process -FilePath $presentMon -ArgumentList $arguments `
    -WorkingDirectory $repo -WindowStyle Hidden -PassThru
$events = [Collections.Generic.List[object]]::new()
Start-Sleep -Seconds 2
for ($index = 0; $index -lt $Cycles; ++$index) {
    $window = (Get-GameProcess).MainWindowHandle
    if ($FocusMode -eq 'Minimize') {
        [P2BoostFocusCycle]::ShowWindowAsync($window, 6) | Out-Null
    } else {
        [P2BoostFocusCycle]::SetForegroundWindow($focusSink.Handle) | Out-Null
    }
    Start-Sleep -Milliseconds 250
    if ($FocusMode -eq 'Minimize') {
        [P2BoostFocusCycle]::ShowWindowAsync($window, 9) | Out-Null
    }
    [P2BoostFocusCycle]::SetForegroundWindow($window) | Out-Null
    $restoreQpc = [Diagnostics.Stopwatch]::GetTimestamp()
    # Let focus/device reacquisition finish so the measured hitch is caused by
    # the first portal work, not by the expected minimized/restored frame.
    Start-Sleep -Milliseconds 750
    $portal = $index % 2
    $x = if ($portal -eq 0) { -1000 } else { -900 }
    $portalQpc = [Diagnostics.Stopwatch]::GetTimestamp()
    & (Join-Path $PSScriptRoot 'invoke-rcon.ps1') -ProcessId $gameProcess.Id `
        -Password $RconPassword -Command @("portal_place 0 $portal $x 0 -271 -90 0 0") | Out-Null
    $events.Add([pscustomobject]@{ cycle=$index; restore_qpc=$restoreQpc; portal_qpc=$portalQpc; portal=$portal })
    Start-Sleep -Milliseconds 450
}
$events | Export-Csv -LiteralPath $eventPath -NoTypeInformation
if ($TraceP2Boost) {
    & (Join-Path $PSScriptRoot 'invoke-rcon.ps1') -ProcessId $gameProcess.Id `
        -Password $RconPassword -Command @('p2boost_dump_metrics', 'p2boost_trace 0') | Out-Null
}
$capture.WaitForExit()
if ($focusSink) { $focusSink.Dispose() }
if ($capture.ExitCode -ne 0) { throw "PresentMon exited with $($capture.ExitCode)" }
Write-Host "Completed $Cycles timestamped focus/portal cycles: $outputPath"
