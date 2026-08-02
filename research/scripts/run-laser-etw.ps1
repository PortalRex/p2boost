param(
    [ValidateSet('LaserRelays', 'SpherePeek')][string]$Scenario = 'LaserRelays',
    [string]$Output = '',
    [string]$RconPassword = 'p2boostlab',
    [ValidateRange(23, 60)][int]$DurationSeconds = 25,
    [switch]$KeepGameOpen
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$capture = Join-Path $PSScriptRoot 'capture-etw.ps1'
$rcon = Join-Path $repo 'scripts\invoke-rcon.ps1'
$focusGame = Join-Path $repo 'scripts\set-game-foreground.ps1'
$stopGame = Join-Path $repo 'scripts\stop-portal2.ps1'
$outputName = if ($Output) {
    $Output
} elseif ($Scenario -eq 'SpherePeek') {
    'artifacts\etw\sphere-peek-route.etl'
} else {
    'artifacts\etw\laser-route.etl'
}
$target = [IO.Path]::GetFullPath((Join-Path $repo $outputName))
$log = [IO.Path]::ChangeExtension($target, '.log')
$errorLog = [IO.Path]::ChangeExtension($target, '.error.log')
$game = Get-Process portal2 -ErrorAction Stop | Select-Object -First 1

function Invoke-Game([string[]]$Commands) {
    return & $rcon -Password $RconPassword -ResponseTimeoutSeconds 10 -Command $Commands
}

function Read-Cvar([string]$Name) {
    $text = Invoke-Game @($Name) | Out-String
    $match = [regex]::Match($text, '=\s*"([^"]+)"')
    if (-not $match.Success) {
        throw "Could not read $Name from: $text"
    }

    return $match.Groups[1].Value
}

$originalFpsMax = Read-Cvar 'fps_max'
$originalCheats = Read-Cvar 'sv_cheats'

# Start from the scenario's real spawn and prepare only the portal gun and
# instrumentation. Route inputs remain collision-bound in both scenarios.
$map = if ($Scenario -eq 'SpherePeek') { 'sp_a2_sphere_peek' } else { 'sp_a2_laser_relays' }
Invoke-Game @("map $map") | Out-Null
Start-Sleep -Seconds 4
Invoke-Game @(
    'sv_cheats 1',
    'fps_max 999',
    'give weapon_portalgun',
    'upgrade_portalgun',
    'p2boost_enable 1'
) | Out-Null

Remove-Item $log, $errorLog -Force -ErrorAction SilentlyContinue
$captureProcess = Start-Process powershell.exe -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput $log -RedirectStandardError $errorLog -ArgumentList @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', $capture,
        '-Output', $target,
        '-Profile', 'GeneralProfile',
        '-DurationSeconds', $DurationSeconds
    )

try {
    # Queue gameplay only after WPR and PresentMon are both recording.
    $deadline = (Get-Date).AddSeconds(15)
    do {
        Start-Sleep -Milliseconds 100
        $text = Get-Content $log -Raw -ErrorAction SilentlyContinue
    } until ($text -match 'Recording for' -or (Get-Date) -ge $deadline -or $captureProcess.HasExited)

    if ($text -notmatch 'Recording for') {
        throw "The ETW capture did not start: $text $(Get-Content $errorLog -Raw -ErrorAction SilentlyContinue)"
    }

    # ETW must observe the normal foreground scheduling/presentation path.
    & $focusGame -ProcessId $game.Id

    $actions = if ($Scenario -eq 'SpherePeek') {
        # Enter the chamber around the panel, take the narrow twelve-tick line
        # onto the faith plate, and retain the catapult launch and landing.
		'wait_for 30 +forward; wait_for 210 -forward; wait_for 210 +moveleft; ' +
		'wait_for 270 -moveleft; wait_for 270 +forward; wait_for 750 -forward; ' +
		'wait_for 750 +moveleft; wait_for 762 -moveleft; wait_for 762 +forward; ' +
		'wait_for 1230 -forward'
    } else {
		'wait_for 30 +forward; wait_for 1200 -forward; ' +
		'wait_for 1212 setang 0 -90 0; wait_for 1218 +attack; wait_for 1224 -attack; ' +
		'wait_for 1254 setang 0 0 0; wait_for 1260 +attack2; wait_for 1266 -attack2; ' +
		'wait_for 1296 setang 0 -90 0'
    }
    Invoke-Game @($actions) | Out-Null

    $captureProcess.WaitForExit()
    $captureProcess.Refresh()
    if ($captureProcess.ExitCode -ne 0) {
        throw "The ETW capture failed with $($captureProcess.ExitCode): $(Get-Content $errorLog -Raw -ErrorAction SilentlyContinue)"
    }
} finally {
    try {
        Invoke-Game @(
            '-forward',
            '-moveleft',
            '-attack',
            '-attack2',
            'p2boost_enable 1',
            "fps_max $originalFpsMax",
            "sv_cheats $originalCheats"
        ) | Out-Null
    } catch {}
    if (-not $captureProcess.HasExited) {
        $captureProcess.Kill()
    }
    if (-not $KeepGameOpen) { & $stopGame -ProcessId $game.Id }
}

Get-Content $log
