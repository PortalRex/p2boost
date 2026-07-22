param(
    [Parameter(Mandatory)][int]$ProcessId,
    [Parameter(Mandatory)][int]$Cycles,
    [Parameter(Mandatory)][string]$EventPath,
    [ValidateSet('AltTab','Minimize')][string]$FocusMode = 'AltTab',
    [string]$RconPassword = 'p2boostlab',
    [switch]$TraceP2Boost
)
$ErrorActionPreference = 'Stop'
$rcon = Join-Path $PSScriptRoot 'invoke-rcon.ps1'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class P2BoostFocusActions {
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
function Get-Game {
    $process = Get-Process -Id $ProcessId -ErrorAction Stop
    if ($process.MainWindowHandle -eq [IntPtr]::Zero) { throw 'Portal 2 has no top-level window.' }
    return $process
}
function Invoke-Game([string[]]$Commands) {
    for ($attempt = 1; $attempt -le 3; ++$attempt) {
        try {
            return & $rcon -ProcessId $ProcessId -Password $RconPassword `
                -Command $Commands -ResponseTimeoutSeconds 10
        } catch {
            if ($attempt -eq 3 -or -not (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue)) { throw }
            Start-Sleep -Milliseconds 250
        }
    }
}
$events = [Collections.Generic.List[object]]::new()
try {
    if ($TraceP2Boost) { Invoke-Game @('p2boost_trace 1') | Out-Null }
    Start-Sleep -Seconds 2
    for ($index = 0; $index -lt $Cycles; ++$index) {
        $window = (Get-Game).MainWindowHandle
        if ($FocusMode -eq 'Minimize') {
            [P2BoostFocusActions]::ShowWindowAsync($window, 6) | Out-Null
        } else {
            [P2BoostFocusActions]::SetForegroundWindow($focusSink.Handle) | Out-Null
        }
        Start-Sleep -Milliseconds 250
        if ($FocusMode -eq 'Minimize') { [P2BoostFocusActions]::ShowWindowAsync($window, 9) | Out-Null }
        [P2BoostFocusActions]::SetForegroundWindow($window) | Out-Null
        $restoreQpc = [Diagnostics.Stopwatch]::GetTimestamp()
        Start-Sleep -Milliseconds 750
        $portal = $index % 2
        $x = if ($portal -eq 0) { -1000 } else { -900 }
        $portalQpc = [Diagnostics.Stopwatch]::GetTimestamp()
        Invoke-Game @("portal_place 0 $portal $x 0 -271 -90 0 0") | Out-Null
        $events.Add([pscustomobject]@{cycle=$index;restore_qpc=$restoreQpc;portal_qpc=$portalQpc;portal=$portal})
        Start-Sleep -Milliseconds 450
    }
    $events | Export-Csv -LiteralPath $EventPath -NoTypeInformation
    if ($TraceP2Boost) { Invoke-Game @('p2boost_dump_metrics', 'p2boost_trace 0') | Out-Null }
} finally {
    if ($TraceP2Boost) { try { Invoke-Game @('p2boost_trace 0') | Out-Null } catch {} }
    if ($focusSink) { $focusSink.Dispose() }
}
