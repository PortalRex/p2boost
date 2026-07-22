param(
    [ValidateSet('Both', 'SinglePlayer', 'Coop')][string]$Scenario = 'Both',
    [string]$OutputDirectory = 'artifacts\benchmarks\live-gameplay',
    [ValidateRange(13, 30)][int]$DurationSeconds = 13,
    [ValidateSet(360, 500, 999)][int]$FpsMax = 999,
    [int]$ProcessId = 0,
    [string]$RconPassword = 'p2boostlab',
    [string]$GameRoot = 'D:\SteamLibrary\steamapps\common\Portal 2'
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$rcon = Join-Path $PSScriptRoot 'invoke-rcon.ps1'
$analyze = Join-Path $PSScriptRoot 'analyze-presentmon.ps1'
$presentMon = Join-Path $repo '.tools\downloads\PresentMon.exe'
$output = [IO.Path]::GetFullPath((Join-Path $repo $OutputDirectory))
$process = if ($ProcessId) {
    Get-Process -Id $ProcessId -ErrorAction Stop
} else {
    Get-Process portal2 -ErrorAction Stop | Select-Object -First 1
}

New-Item -ItemType Directory -Force $output | Out-Null
if (-not (Test-Path -LiteralPath $presentMon)) {
    throw 'Run scripts\bootstrap-tools.ps1 before live gameplay benchmarks.'
}

# Restoring and foregrounding the window keeps PresentMon on the same visible
# presentation path for every sample. This does not alter game configuration.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class P2BoostGameplayForeground {
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
}
'@

function Invoke-Game([string[]]$Commands) {
    # A level transition may briefly stop answering RCON while the listen server
    # remains alive. Retrying the complete packet is safe because these commands
    # are deliberately idempotent or immediately balanced by a release command.
    for ($attempt = 1; $attempt -le 3; ++$attempt) {
        try {
            return & $rcon -ProcessId $process.Id -Password $RconPassword `
                -ResponseTimeoutSeconds 10 -Command $Commands
        } catch {
            if ($attempt -eq 3 -or -not (Get-Process -Id $process.Id -ErrorAction SilentlyContinue)) {
                throw
            }

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

function Wait-ForSession([string]$Map, [int]$Users) {
    # Poll the authoritative server status rather than sleeping for an assumed
    # load time. This keeps the benchmark short on fast machines and reliable
    # when a map takes longer to initialize.
    $deadline = (Get-Date).AddSeconds(15)
    do {
        Start-Sleep -Milliseconds 250
        try {
            $status = Invoke-Game @('users', 'status') | Out-String
            if ($status -match "(?m)^$Users users\s*$" -and $status -match "map\s+:\s+$Map") {
                return
            }
        } catch {
            # RCON is expected to be unavailable for a small part of a load.
        }
    } until ((Get-Date) -ge $deadline)

    throw "$Map did not reach $Users active player(s)."
}

function Set-Foreground {
    # SW_RESTORE also recovers a minimized game before collecting presentation
    # data, which avoids measuring Windows' occluded-window throttling.
    [P2BoostGameplayForeground]::ShowWindow($process.MainWindowHandle, 9) | Out-Null
    [P2BoostGameplayForeground]::SetForegroundWindow($process.MainWindowHandle) | Out-Null
    Start-Sleep -Milliseconds 250
}

function Save-Proof([string]$Map, [string]$Name) {
    $issued = Get-Date
    Invoke-Game @('jpeg') | Out-Null

    # Source defers JPEG encoding to a rendered frame, so wait for a non-empty
    # file newer than the command instead of copying a stale screenshot.
    $deadline = (Get-Date).AddSeconds(5)
    do {
        $shot = Get-ChildItem (Join-Path $GameRoot 'portal2\screenshots') -Filter "$Map*.jpg" |
            Where-Object { $_.Length -gt 0 -and $_.LastWriteTime -ge $issued.AddSeconds(-1) } |
            Sort-Object LastWriteTime | Select-Object -Last 1
        if ($shot) {
            Copy-Item -LiteralPath $shot.FullName -Destination (Join-Path $output "$Name-proof.jpg") -Force
            return
        }

        Start-Sleep -Milliseconds 100
    } until ((Get-Date) -ge $deadline)

    throw "The $Name proof screenshot was not written."
}

function Set-SinglePlayerScene {
    Invoke-Game @('map sp_a2_laser_relays') | Out-Null
    Wait-ForSession 'sp_a2_laser_relays' 1

    Invoke-Game @(
        'sv_cheats 1',
        "fps_max $FpsMax",
        'give weapon_portalgun',
        'upgrade_portalgun'
    ) | Out-Null

    Set-Foreground
}

function Set-CoopScene {
    $session = Invoke-Game @('users', 'status') | Out-String
    if ($session -notmatch '(?m)^2 users\s*$' -or $session -notmatch 'map\s+:\s+mp_coop_lobby_2') {
        Invoke-Game @('ss_map mp_coop_lobby_2') | Out-Null
        Wait-ForSession 'mp_coop_lobby_2' 2
    }

    Invoke-Game @(
        'sv_cheats 1',
        "fps_max $FpsMax",
        'cmd1 give weapon_portalgun', 'cmd1 upgrade_portalgun',
        'cmd2 give weapon_portalgun', 'cmd2 upgrade_portalgun',
        'cmd1 setpos 6192 3850 -480', 'cmd1 setang 0 90 0',
        'cmd2 setpos 6250 3850 -480', 'cmd2 setang 0 90 0',
        # The known lobby wall keeps both linked portals visible in split-screen.
        'portal_place 0 0 6144 4160 -416 0 -90 0',
        'portal_place 0 1 6240 4160 -416 0 -90 0'
    ) | Out-Null

    Set-Foreground
}

function Invoke-SinglePlayerActions {
    # Schedule real inputs before PresentMon starts. Sending one RCON packet per
    # action during a capture produced a false 33.7 ms main-thread hitch.
    Invoke-Game @(
        # Laser Relays opens into its expensive main room by walking straight
        # from the normal spawn. No position, view, or collision cheats are
        # used, so the capture includes the real visibility transition.
        'hwait 90 cmd1 +forward; hwait 300 cmd1 +attack; hwait 306 cmd1 -attack; ' +
        'hwait 330 cmd1 +attack2; hwait 336 cmd1 -attack2; hwait 720 cmd1 -forward'
    ) | Out-Null
}

function Invoke-CoopActions {
    # Both local players move and shoot while normal prediction, simulation,
    # visibility, particles, and split-screen rendering remain active.
    Invoke-Game @(
        'hwait 90 cmd1 +forward; hwait 90 cmd1 +moveleft; hwait 90 cmd2 +forward; hwait 90 cmd2 +moveright; ' +
        'hwait 129 cmd1 -moveleft; hwait 129 cmd2 -moveright; hwait 129 cmd1 +jump; hwait 129 cmd2 +jump; ' +
        'hwait 138 cmd1 -jump; hwait 138 cmd2 -jump; hwait 138 cmd1 +attack; hwait 138 cmd2 +attack2; ' +
        'hwait 144 cmd1 -attack; hwait 144 cmd2 -attack2; hwait 183 cmd1 -forward; hwait 183 cmd2 -forward'
    ) | Out-Null
}

function Stop-AllInputs {
    # Always release every input even when capture or RCON fails, otherwise an
    # interrupted run can leave a player moving during the next measurement.
    Invoke-Game @(
        'cmd1 -forward', 'cmd1 -back', 'cmd1 -moveleft', 'cmd1 -moveright',
        'cmd1 -jump', 'cmd1 -attack', 'cmd1 -attack2',
        'cmd2 -forward', 'cmd2 -back', 'cmd2 -moveleft', 'cmd2 -moveright',
        'cmd2 -jump', 'cmd2 -attack', 'cmd2 -attack2'
    ) | Out-Null
}

function Measure-Scene([string]$Name, [bool]$Patched, [scriptblock]$Setup, [scriptblock]$Actions) {
    & $Setup
    Invoke-Game @("p2boost_enable $([int]$Patched)") | Out-Null
    Start-Sleep -Milliseconds 300

    $variant = if ($Patched) { 'patched' } else { 'unpatched' }
    $csv = Join-Path $output "$Name-$variant.csv"

    if (Test-Path -LiteralPath $csv) {
        Remove-Item -LiteralPath $csv -Force
    }

    # Queue every gameplay action before measurement. The first action has a
    # 1.5-second lead-in, leaving enough time for PresentMon to attach without
    # allowing RCON parsing or socket activity into the measured interval.
    & $Actions

    # Keep PresentMon in this elevated process tree. A PowerShell background job
    # starts through remoting and loses the caller's usable elevation token on
    # some Windows installations, producing an empty capture despite admin UI.
    $captureProcess = Start-Process -FilePath $presentMon -NoNewWindow -PassThru -ArgumentList @(
        '--process_name', 'portal2.exe',
        '--output_file', $csv,
        '--v2_metrics',
        # Recover a trace session left behind by an interrupted benchmark.
        '--stop_existing_session',
        '--timed', $DurationSeconds,
        '--terminate_after_timed',
        '--no_console_stats',
        '--set_circular_buffer_size', '32768'
    )

    try {
        $captureProcess.WaitForExit()
        $captureProcess.Refresh()
        # Windows PowerShell can discard ExitCode after the native handle is
        # released even though WaitForExit completed; the CSV validation below
        # remains authoritative in that case.
        if ($null -ne $captureProcess.ExitCode -and $captureProcess.ExitCode -ne 0) {
            throw "PresentMon exited with $($captureProcess.ExitCode)."
        }

        if (-not (Test-Path -LiteralPath $csv) -or (Get-Item -LiteralPath $csv).Length -eq 0) {
            throw "PresentMon produced no frames: $csv"
        }
    } finally {
        try { Stop-AllInputs } catch {}
        if (-not $captureProcess.HasExited) {
            $captureProcess.Kill()
        }
    }

    & $analyze -InputCsv $csv
}

$originalFpsMax = Read-Cvar 'fps_max'
$originalCheats = Read-Cvar 'sv_cheats'

try {
    $runtime = Invoke-Game @('p2boost_status') | Out-String
    if ($runtime -notmatch 'supported=1' -or $runtime -notmatch 'native_d3d9=1') {
        throw "p2boost runtime verification failed: $runtime"
    }

    # Run co-op first when both tracks are requested. Source can leave a live
    # single-player client disconnected after an in-process `ss_map`, while a
    # normal `map` transition from an existing split-screen server is reliable.
    if ($Scenario -in @('Both', 'Coop')) {
        if ($Scenario -eq 'Both') {
            $session = Invoke-Game @('users', 'status') | Out-String
            if ($session -notmatch '(?m)^2 users\s*$') {
                throw 'Launch Portal 2 with +ss_map mp_coop_lobby_2 before a combined SP/co-op run.'
            }
        }

        Measure-Scene 'coop-lobby' $false ${function:Set-CoopScene} ${function:Invoke-CoopActions}
        Measure-Scene 'coop-lobby' $true ${function:Set-CoopScene} ${function:Invoke-CoopActions}
        Save-Proof 'mp_coop_lobby_2' 'coop-lobby'
    }

    if ($Scenario -in @('Both', 'SinglePlayer')) {
        Measure-Scene 'sp-laser-relays' $false ${function:Set-SinglePlayerScene} ${function:Invoke-SinglePlayerActions}
        Measure-Scene 'sp-laser-relays' $true ${function:Set-SinglePlayerScene} ${function:Invoke-SinglePlayerActions}
        Save-Proof 'sp_a2_laser_relays' 'sp-laser-relays'
    }
} finally {
    try { Stop-AllInputs } catch {}
    try {
        Invoke-Game @(
            'p2boost_trace 0',
            'p2boost_enable 1',
            "fps_max $originalFpsMax",
            "sv_cheats $originalCheats"
        ) | Out-Null
    } catch {}
}
