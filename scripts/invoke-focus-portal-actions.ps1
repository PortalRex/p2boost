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

function Read-Cvar([string]$Name) {
    $text = Invoke-Game @($Name) | Out-String
    $match = [regex]::Match($text, '=\s*"([^"]+)"')
    if (-not $match.Success) {
        throw "Could not read $Name from: $text"
    }

    return $match.Groups[1].Value
}

$originalCheats = Read-Cvar 'sv_cheats'
$events = [Collections.Generic.List[object]]::new()
try {
    if ($TraceP2Boost) { Invoke-Game @('p2boost_trace 1') | Out-Null }

    # Use a known pair of portalable walls in Laser Relays. Positioning is part
    # of test setup only; each measured portal is still fired by the portal gun.
    Invoke-Game @(
        'sv_cheats 1',
        'give weapon_portalgun',
        'upgrade_portalgun',
        'setpos -687.968750 -707.304016 64.031250',
        'setang 0 -90 0',
        'ent_fire prop_portal fizzle'
    ) | Out-Null

    Start-Sleep -Seconds 2
    for ($index = 0; $index -lt $Cycles; ++$index) {
        # Remove both old portals before focus is lost. The delayed commands
        # below execute entirely inside the engine, avoiding an RCON parse in
        # the frame whose first newly connected portal begins rendering.
        Invoke-Game @(
            'ent_fire prop_portal fizzle',
            'setang 0 -90 0',
            'hwait 75 +attack; hwait 81 -attack; ' +
            'hwait 99 setang 0 0 0; hwait 105 +attack2; hwait 111 -attack2; ' +
            'hwait 129 setang 0 -90 0'
        ) | Out-Null
        $scheduleQpc = [Diagnostics.Stopwatch]::GetTimestamp()

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

        # hwait is expressed in 60 Hz simulation ticks. The second shot is the
        # instant at which the first connected pair becomes renderable.
        $primaryQpc = $scheduleQpc + 75.0 / 60.0 * [Diagnostics.Stopwatch]::Frequency
        $connectedQpc = $scheduleQpc + 105.0 / 60.0 * [Diagnostics.Stopwatch]::Frequency
        $events.Add([pscustomobject]@{
            cycle = $index
            restore_qpc = $restoreQpc
            primary_qpc = $primaryQpc
            portal_qpc = $connectedQpc
        })
        Start-Sleep -Milliseconds 2200
    }
    $events | Export-Csv -LiteralPath $EventPath -NoTypeInformation
    if ($TraceP2Boost) { Invoke-Game @('p2boost_dump_metrics', 'p2boost_trace 0') | Out-Null }
} finally {
    if ($TraceP2Boost) { try { Invoke-Game @('p2boost_trace 0') | Out-Null } catch {} }
    try { Invoke-Game @("sv_cheats $originalCheats") | Out-Null } catch {}
    if ($focusSink) { $focusSink.Dispose() }
}
