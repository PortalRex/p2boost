param(
    [string]$Output = 'artifacts\etw\laser-route.etl',
    [string]$RconPassword = 'p2boostlab',
    [ValidateRange(24, 60)][int]$DurationSeconds = 25
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$capture = Join-Path $PSScriptRoot 'capture-etw.ps1'
$rcon = Join-Path $PSScriptRoot 'invoke-rcon.ps1'
$target = [IO.Path]::GetFullPath((Join-Path $repo $Output))
$log = [IO.Path]::ChangeExtension($target, '.log')
$errorLog = [IO.Path]::ChangeExtension($target, '.error.log')

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

# Start from the real spawn and prepare only the portal gun and instrumentation.
Invoke-Game @('map sp_a2_laser_relays') | Out-Null
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

    Invoke-Game @(
        'hwait 30 +forward; hwait 1200 -forward; ' +
        'hwait 1212 setang 0 -90 0; hwait 1218 +attack; hwait 1224 -attack; ' +
        'hwait 1254 setang 0 0 0; hwait 1260 +attack2; hwait 1266 -attack2; ' +
        'hwait 1296 setang 0 -90 0'
    ) | Out-Null

    $captureProcess.WaitForExit()
    $captureProcess.Refresh()
    if ($captureProcess.ExitCode -ne 0) {
        throw "The ETW capture failed with $($captureProcess.ExitCode): $(Get-Content $errorLog -Raw -ErrorAction SilentlyContinue)"
    }
} finally {
    try {
        Invoke-Game @(
            '-forward',
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
}

Get-Content $log
