param(
    [Parameter(Mandatory)][int]$ProcessId,
    [Parameter(Mandatory)][int]$Cycles,
    [Parameter(Mandatory)][string]$EventPath,
    [ValidateSet('AltTab','Minimize')][string]$FocusMode = 'AltTab',
    [string]$RconPassword = 'p2boostlab',
    [switch]$TraceP2Boost,
    [switch]$QuickLifecycle
)
$ErrorActionPreference = 'Stop'
$rcon = Join-Path $PSScriptRoot 'invoke-rcon.ps1'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class P2BoostFocusActions {
    [DllImport("user32.dll")] public static extern bool ShowWindowAsync(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint first, uint second, bool attach);
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

function Set-VerifiedForeground(
    [IntPtr]$Window,
    [IntPtr]$DisallowedWindow = [IntPtr]::Zero
) {
    # A hidden PowerShell helper is not automatically allowed to steal focus
    # back from its own sink window. Join the input queues only for the focus
    # operation, then prove that Windows accepted the transition.
    $ignoredProcess = 0
    $targetThread = [P2BoostFocusActions]::GetWindowThreadProcessId(
        $Window, [ref]$ignoredProcess)
    $currentThread = [P2BoostFocusActions]::GetCurrentThreadId()
    $foreground = [P2BoostFocusActions]::GetForegroundWindow()
    $foregroundThread = if ($foreground -ne [IntPtr]::Zero) {
        [P2BoostFocusActions]::GetWindowThreadProcessId(
            $foreground, [ref]$ignoredProcess)
    } else {
        0
    }

    $attachedTarget = $targetThread -ne 0 -and $targetThread -ne $currentThread -and
        [P2BoostFocusActions]::AttachThreadInput($currentThread, $targetThread, $true)
    $attachedForeground = $foregroundThread -ne 0 -and
        $foregroundThread -ne $currentThread -and $foregroundThread -ne $targetThread -and
        [P2BoostFocusActions]::AttachThreadInput($currentThread, $foregroundThread, $true)
    try {
        [P2BoostFocusActions]::ShowWindowAsync($Window, 9) | Out-Null
        [P2BoostFocusActions]::BringWindowToTop($Window) | Out-Null
        [P2BoostFocusActions]::SetForegroundWindow($Window) | Out-Null
    } finally {
        if ($attachedForeground) {
            [P2BoostFocusActions]::AttachThreadInput(
                $currentThread, $foregroundThread, $false) | Out-Null
        }
        if ($attachedTarget) {
            [P2BoostFocusActions]::AttachThreadInput(
                $currentThread, $targetThread, $false) | Out-Null
        }
    }

    $deadline = (Get-Date).AddSeconds(1)
    do {
        $actual = [P2BoostFocusActions]::GetForegroundWindow()
        if ($actual -eq $Window -or
            ($DisallowedWindow -ne [IntPtr]::Zero -and $actual -ne $DisallowedWindow)) {
            return
        }
        Start-Sleep -Milliseconds 20
    } until ((Get-Date) -ge $deadline)
    throw "Windows did not foreground window $Window."
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

# The release gate can repeat the exact focus/resource lifecycle quickly. It
# still fires both real portal-gun inputs after restore, but avoids two seconds
# of idle time between cycles.
$primaryDown = if ($QuickLifecycle) { 6 } else { 75 }
$primaryUp = if ($QuickLifecycle) { 10 } else { 81 }
$secondaryAngle = if ($QuickLifecycle) { 12 } else { 99 }
$secondaryDown = if ($QuickLifecycle) { 18 } else { 105 }
$secondaryUp = if ($QuickLifecycle) { 22 } else { 111 }
$cameraRestore = if ($QuickLifecycle) { 28 } else { 129 }
$cycleSettleMilliseconds = if ($QuickLifecycle) { 900 } else { 2200 }
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
        # Remove both old portals before focus is lost. The portal inputs are
        # scheduled only after a verified restore, so a device-reset pause
        # cannot shift their simulation ticks away from the recorded QPC.
        Invoke-Game @(
            'ent_fire prop_portal fizzle',
            'setang 0 -90 0'
        ) | Out-Null

        $window = (Get-Game).MainWindowHandle
        if ($FocusMode -eq 'Minimize') {
            [P2BoostFocusActions]::ShowWindowAsync($window, 6) | Out-Null
        } else {
            # Windows may activate the helper's host rather than its hidden
            # sink. Either is a valid focus-loss event as long as Portal 2 is
            # no longer foreground.
            Set-VerifiedForeground $focusSink.Handle $window
        }
        Start-Sleep -Milliseconds 250
        Set-VerifiedForeground $window
        $restoreQpc = [Diagnostics.Stopwatch]::GetTimestamp()

        # The RCON parse finishes well before either shot. wait_for is
        # expressed in 60 Hz session ticks, and the second shot is the
        # instant at which the first connected pair becomes renderable.
        Invoke-Game @(
            "wait_for $primaryDown +attack; wait_for $primaryUp -attack; " +
            "wait_for $secondaryAngle setang 0 0 0; wait_for $secondaryDown +attack2; " +
            "wait_for $secondaryUp -attack2; wait_for $cameraRestore setang 0 -90 0"
        ) | Out-Null
        $scheduleQpc = [Diagnostics.Stopwatch]::GetTimestamp()
        $primaryQpc = $scheduleQpc + $primaryDown / 60.0 * [Diagnostics.Stopwatch]::Frequency
        $connectedQpc = $scheduleQpc + $secondaryDown / 60.0 * [Diagnostics.Stopwatch]::Frequency
        $events.Add([pscustomobject]@{
            cycle = $index
            restore_qpc = $restoreQpc
            primary_qpc = $primaryQpc
            portal_qpc = $connectedQpc
        })
        Start-Sleep -Milliseconds $cycleSettleMilliseconds
    }
    $events | Export-Csv -LiteralPath $EventPath -NoTypeInformation
    if ($TraceP2Boost) { Invoke-Game @('p2boost_dump_metrics', 'p2boost_trace 0') | Out-Null }
} finally {
    if ($TraceP2Boost) { try { Invoke-Game @('p2boost_trace 0') | Out-Null } catch {} }
    try { Invoke-Game @("sv_cheats $originalCheats") | Out-Null } catch {}
    if ($focusSink) { $focusSink.Dispose() }
}
