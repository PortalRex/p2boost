param(
    [string]$Output = 'artifacts\profiles\sphere-peek-main-thread.csv',
    [ValidateRange(5, 30)][int]$DurationSeconds = 13,
    [switch]$Stack,
    [string]$RconPassword = 'p2boostlab'
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$rcon = Join-Path $PSScriptRoot 'invoke-rcon.ps1'
$focusGame = Join-Path $PSScriptRoot 'set-game-foreground.ps1'
$profiler = Join-Path $repo 'build\vs2022-x86\Release\p2profile.exe'
$target = [IO.Path]::GetFullPath((Join-Path $repo $Output))
$game = Get-Process portal2 -ErrorAction Stop | Select-Object -First 1

if (-not (Test-Path -LiteralPath $profiler)) {
    throw 'p2profile.exe is missing; build the Release configuration first.'
}

New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
Remove-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue

function Invoke-Game([string[]]$Commands) {
    return & $rcon -ProcessId $game.Id -Password $RconPassword `
        -ResponseTimeoutSeconds 10 -Command $Commands
}

# Reloading makes every sample start from the same entity and visibility state.
Invoke-Game @('map sp_a2_sphere_peek') | Out-Null
Start-Sleep -Seconds 4
Invoke-Game @(
    'sv_cheats 1',
    'fps_max 999',
    'give weapon_portalgun',
    'upgrade_portalgun',
    'p2boost_enable 1',
    '-forward',
    '-moveleft'
) | Out-Null

# EIP-only sampling is the low-overhead default. Stack mode is available for
# short follow-up captures after an instruction range has been identified.
$captureArguments = @(
    $game.Id,
    $DurationSeconds,
    $target
)
if ($Stack) { $captureArguments += '--stack' }
$capture = Start-Process -FilePath $profiler -NoNewWindow -PassThru `
    -ArgumentList $captureArguments

try {
    Start-Sleep -Milliseconds 100
    & $focusGame -ProcessId $game.Id

    # Move around the entrance panel, enter the chamber, then shift left for
    # exactly twelve ticks. This is the narrow route that hits the catapult.
    Invoke-Game @(
        'hwait 60 +forward; hwait 240 -forward; hwait 240 +moveleft; ' +
        'hwait 300 -moveleft; hwait 300 +forward; hwait 780 -forward; ' +
        'hwait 780 +moveleft; hwait 792 -moveleft; hwait 792 +forward; ' +
        'hwait 1260 -forward'
    ) | Out-Null

    $capture.WaitForExit()
    $capture.Refresh()
    # Some Windows PowerShell builds do not populate ExitCode for a process
    # attached to this console. The output file remains the authoritative
    # completion artifact in that case.
    if ($null -ne $capture.ExitCode -and $capture.ExitCode -ne 0) {
        throw "p2profile failed with exit code $($capture.ExitCode)."
    }
    if (-not (Test-Path -LiteralPath $target) -or (Get-Item $target).Length -eq 0) {
        throw 'p2profile did not produce a sample file.'
    }
} finally {
    try { Invoke-Game @('-forward', '-moveleft') | Out-Null } catch {}
    if (-not $capture.HasExited) { $capture.Kill() }
}

Get-Item -LiteralPath $target
