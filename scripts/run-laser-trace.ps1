param(
    [string]$OutputDirectory = 'artifacts\benchmarks\laser-strike-trace',
    [ValidateRange(20, 35)][int]$DurationSeconds = 24,
    [ValidateSet(360, 500, 999)][int]$FpsMax = 999,
    [string]$RconPassword = 'p2boostlab',
    [string]$GameRoot = 'D:\SteamLibrary\steamapps\common\Portal 2',
    [switch]$AlreadyForeground,
    [switch]$KeepGameOpen
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$rcon = Join-Path $PSScriptRoot 'invoke-rcon.ps1'
$analyze = Join-Path $PSScriptRoot 'analyze-presentmon.ps1'
$analyzeHitches = Join-Path $PSScriptRoot 'analyze-hitches.ps1'
$focusGame = Join-Path $PSScriptRoot 'set-game-foreground.ps1'
$stopGame = Join-Path $PSScriptRoot 'stop-portal2.ps1'
$presentMon = Join-Path $repo '.tools\downloads\PresentMon.exe'
$output = [IO.Path]::GetFullPath((Join-Path $repo $OutputDirectory))
$game = Get-Process portal2 -ErrorAction Stop | Select-Object -First 1

New-Item -ItemType Directory -Force $output | Out-Null

function Invoke-Game([string[]]$Commands) {
    # Map loads briefly stop the listen server from answering RCON.
    for ($attempt = 1; $attempt -le 3; ++$attempt) {
        try {
            return & $rcon -ProcessId $game.Id -Password $RconPassword `
                -ResponseTimeoutSeconds 10 -Command $Commands
        } catch {
            if ($attempt -eq 3 -or -not (Get-Process -Id $game.Id -ErrorAction SilentlyContinue)) {
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

function Wait-ForMap {
    # Poll the server instead of assuming a fixed load duration.
    $deadline = (Get-Date).AddSeconds(15)
    do {
        Start-Sleep -Milliseconds 250
        try {
            $status = Invoke-Game @('users', 'status') | Out-String
            if ($status -match '(?m)^1 users\s*$' -and $status -match 'map\s+:\s+sp_a2_laser_relays') {
                return
            }
        } catch {
            # A short RCON outage is normal while Source changes levels.
        }
    } until ((Get-Date) -ge $deadline)

    throw 'sp_a2_laser_relays did not become playable.'
}

function Stop-Inputs {
    Invoke-Game @(
        '-forward', '-back', '-moveleft', '-moveright',
        '-jump', '-attack', '-attack2'
    ) | Out-Null
}

$presentCsv = Join-Path $output 'laser-strike.csv'
$metricsCsv = Join-Path $output 'laser-strike-metrics.csv'
$workersCsv = Join-Path $output 'laser-strike-render-workers.csv'
$originalFpsMax = Read-Cvar 'fps_max'
$originalCheats = Read-Cvar 'sv_cheats'
$capture = $null

try {
    Invoke-Game @('map sp_a2_laser_relays') | Out-Null
    Wait-ForMap
    Invoke-Game @(
        'sv_cheats 1',
        "fps_max $FpsMax",
        'give weapon_portalgun',
        'upgrade_portalgun',
        'p2boost_enable 1',
        'p2boost_trace 1'
    ) | Out-Null

    Remove-Item $presentCsv, $metricsCsv, $workersCsv -Force -ErrorAction SilentlyContinue
    if (-not $AlreadyForeground) {
        & $focusGame -ProcessId $game.Id
    }

    # Queue collision-bound gameplay before capture so RCON work cannot become
    # a false main-thread hitch. This rides the lift, enters the expensive room,
    # fires both portals, and leaves a connected portal visible.
    Invoke-Game @(
        'wait_for 90 +forward; wait_for 1200 -forward; ' +
        'wait_for 1212 setang 0 -90 0; wait_for 1218 +attack; wait_for 1224 -attack; ' +
        'wait_for 1254 setang 0 0 0; wait_for 1260 +attack2; wait_for 1266 -attack2; ' +
        'wait_for 1296 setang 0 -90 0'
    ) | Out-Null

    $capture = Start-Process -FilePath $presentMon -NoNewWindow -PassThru -ArgumentList @(
        '--process_id', $game.Id,
        '--output_file', $presentCsv,
        '--v2_metrics',
        '--qpc_time',
        '--stop_existing_session',
        '--timed', $DurationSeconds,
        '--terminate_after_timed',
        '--no_console_stats',
        '--set_circular_buffer_size', '32768'
    )
    $capture.WaitForExit()

    if (-not (Test-Path -LiteralPath $presentCsv) -or (Get-Item $presentCsv).Length -eq 0) {
        throw 'PresentMon produced no Laser Relays frames.'
    }

    Invoke-Game @('p2boost_dump_metrics', 'p2boost_trace 0') | Out-Null
    Copy-Item -LiteralPath (Join-Path $GameRoot 'p2boost_metrics.csv') `
        -Destination $metricsCsv -Force

    $gameWorkers = Join-Path $GameRoot 'p2boost_metrics_render_workers.csv'
    if (Test-Path -LiteralPath $gameWorkers) {
        Copy-Item -LiteralPath $gameWorkers -Destination $workersCsv -Force
    }

    & $analyze -InputCsv $presentCsv
    & $analyzeHitches -InputCsv $presentCsv -MetricsCsv $metricsCsv
} finally {
    try { Stop-Inputs } catch {}
    try {
        Invoke-Game @(
            'p2boost_trace 0',
            'p2boost_enable 1',
            "fps_max $originalFpsMax",
            "sv_cheats $originalCheats"
        ) | Out-Null
    } catch {}

    if ($capture -and -not $capture.HasExited) {
        $capture.Kill()
    }
    if (-not $KeepGameOpen) {
        & $stopGame -ProcessId $game.Id
    }
}
