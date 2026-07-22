param(
    [string]$OutputPrefix = 'artifacts\benchmarks\coop-lobby-portals',
    [ValidateRange(5,600)][int]$DurationSeconds = 10,
    [int]$FpsMax = 999,
    [int]$ProcessId = 0,
    [string]$RconPassword = 'p2boostlab',
    [switch]$SetupOnly,
    [string]$GameRoot = 'D:\SteamLibrary\steamapps\common\Portal 2'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$rcon = Join-Path $PSScriptRoot 'invoke-rcon.ps1'
$capture = Join-Path $PSScriptRoot 'capture-presentmon.ps1'
$process = if ($ProcessId) { Get-Process -Id $ProcessId -ErrorAction Stop } else {
    Get-Process portal2 -ErrorAction Stop | Select-Object -First 1
}
$prefix = [IO.Path]::GetFullPath((Join-Path $repo $OutputPrefix))
New-Item -ItemType Directory -Force (Split-Path $prefix) | Out-Null

function Invoke-Game([string[]]$Commands) {
    & $rcon -ProcessId $process.Id -Password $RconPassword -Command $Commands
}
function Read-Cvar([string]$Name) {
    $text = (Invoke-Game @($Name) | Out-String)
    $match = [regex]::Match($text, '=\s*"([^"]+)"')
    if (-not $match.Success) { throw "Could not read $Name from: $text" }
    return $match.Groups[1].Value
}

$originalFpsMax = Read-Cvar 'fps_max'
$originalCheats = Read-Cvar 'sv_cheats'
try {
    $session = Invoke-Game @('users', 'status') | Out-String
    if ($session -notmatch '(?m)^2 users\s*$' -or $session -notmatch 'map\s+:\s+mp_coop_lobby_2') {
        Invoke-Game @('ss_map mp_coop_lobby_2') | Out-Null
        Start-Sleep -Seconds 6
    }
    $deadline = (Get-Date).AddSeconds(30)
    do {
        $status = Invoke-Game @('users', 'status') | Out-String
        if ($status -match '(?m)^2 users\s*$' -and $status -match 'map\s+:\s+mp_coop_lobby_2') { break }
        Start-Sleep -Seconds 1
    } until ((Get-Date) -ge $deadline)
    if ($status -notmatch '(?m)^2 users\s*$' -or $status -notmatch 'map\s+:\s+mp_coop_lobby_2') {
        throw 'Local split-screen lobby did not reach two active players.'
    }

    $jpegIssued = Get-Date
    Invoke-Game @(
        'sv_cheats 1',
        "fps_max $FpsMax",
        'cmd1 give weapon_portalgun', 'cmd1 upgrade_portalgun',
        'cmd2 give weapon_portalgun', 'cmd2 upgrade_portalgun'
    ) | Out-Null
    Start-Sleep -Seconds 1
    Invoke-Game @(
        'cmd1 noclip', 'cmd2 noclip',
        'cmd1 setpos 6192 3850 -480', 'cmd1 setang 0 90 0',
        'cmd2 setpos 6250 3850 -480', 'cmd2 setang 0 90 0'
    ) | Out-Null
    Start-Sleep -Milliseconds 500
    Invoke-Game @('portal_place 0 0 6144 4160 -416 0 -90 0') | Out-Null
    Start-Sleep -Milliseconds 500
    Invoke-Game @('portal_place 0 1 6240 4160 -416 0 -90 0') | Out-Null
    Start-Sleep -Milliseconds 500
    # Placement can teleport a camera when the portal volume initially overlaps
    # its hull, so restore the deterministic view after both portals are linked.
    Invoke-Game @(
        'cmd1 setpos 6192 3850 -480', 'cmd1 setang 0 90 0',
        'cmd2 setpos 6250 3850 -480', 'cmd2 setang 0 90 0',
        'p2boost_trace 0', 'jpeg'
    ) | Out-Null
    Start-Sleep -Seconds 2

    $shotDeadline = (Get-Date).AddSeconds(5)
    do {
        $shot = Get-ChildItem (Join-Path $GameRoot 'portal2\screenshots') -Filter 'mp_coop_lobby_2*.jpg' |
            Where-Object { $_.Length -gt 0 -and $_.LastWriteTime -ge $jpegIssued.AddSeconds(-1) } |
            Sort-Object LastWriteTime | Select-Object -Last 1
        if ($shot) { break }
        Start-Sleep -Milliseconds 100
    } until ((Get-Date) -ge $shotDeadline)
    if (-not $shot) { throw 'Lobby proof screenshot was not written.' }
    Copy-Item -LiteralPath $shot.FullName -Destination "$prefix-proof.jpg" -Force

    $runtime = Invoke-Game @('p2boost_status') | Out-String
    if ($runtime -notmatch 'supported=1' -or $runtime -notmatch 'native_d3d9=1') {
        throw "p2boost runtime verification failed: $runtime"
    }
    if ($SetupOnly) {
        Write-Host "Lobby portal scene ready: pid=$($process.Id) proof=$prefix-proof.jpg"
        return
    }
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class P2BoostLobbyForeground {
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
}
'@
    [P2BoostLobbyForeground]::ShowWindow($process.MainWindowHandle, 9) | Out-Null
    [P2BoostLobbyForeground]::SetForegroundWindow($process.MainWindowHandle) | Out-Null
    Start-Sleep -Milliseconds 500
    Invoke-Game @('p2boost_enable 0') | Out-Null
    & $capture -Output "$prefix-unpatched.csv" -DurationSeconds $DurationSeconds
    Invoke-Game @('p2boost_enable 1') | Out-Null
    & $capture -Output "$prefix-patched.csv" -DurationSeconds $DurationSeconds

    Invoke-Game @('p2boost_trace 1') | Out-Null
    Start-Sleep -Seconds 2
    Invoke-Game @('p2boost_dump_metrics', 'p2boost_trace 0') | Out-Null
    Copy-Item -LiteralPath (Join-Path $GameRoot 'p2boost_metrics.csv') `
        -Destination "$prefix-metrics.csv" -Force
    Write-Host "Lobby portal A/B complete: $prefix-{unpatched,patched,metrics,proof}"
} finally {
    try { Invoke-Game @('p2boost_trace 0', 'p2boost_enable 1', "fps_max $originalFpsMax", "sv_cheats $originalCheats") | Out-Null } catch {}
}
