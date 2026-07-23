param(
    [string]$OutputDirectory = 'artifacts\benchmarks\sphere-peek',
    [ValidateRange(23, 40)][int]$DurationSeconds = 23,
    [ValidateSet(360, 500, 999)][int]$FpsMax = 999,
    [ValidateSet('Both', 'Patched', 'Unpatched')][string]$Variant = 'Both',
	[ValidateRange(1, 10)][int]$Repeats = 1,
	[switch]$SampleMainThread,
	[ValidateRange(0, 65535)][int]$SampleThreadId = 0,
	[switch]$SampleStacks,
    [string]$RconPassword = 'p2boostlab',
    [string]$GameRoot = 'D:\SteamLibrary\steamapps\common\Portal 2'
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$rcon = Join-Path $PSScriptRoot 'invoke-rcon.ps1'
$analyze = Join-Path $PSScriptRoot 'analyze-presentmon.ps1'
$analyzeHitches = Join-Path $PSScriptRoot 'analyze-hitches.ps1'
$focusGame = Join-Path $PSScriptRoot 'set-game-foreground.ps1'
$presentMon = Join-Path $repo '.tools\downloads\PresentMon.exe'
$profiler = Join-Path $repo 'build\vs2022-x86\Release\p2profile.exe'
$output = [IO.Path]::GetFullPath((Join-Path $repo $OutputDirectory))
$game = Get-Process portal2 -ErrorAction Stop | Select-Object -First 1

New-Item -ItemType Directory -Force $output | Out-Null

function Invoke-Game([string[]]$Commands) {
    for ($attempt = 1; $attempt -le 3; ++$attempt) {
        try {
            return & $rcon -ProcessId $game.Id -Password $RconPassword `
                -ResponseTimeoutSeconds 10 -Command $Commands
        } catch {
            if ($attempt -eq 3) { throw }
            Start-Sleep -Milliseconds 250
        }
    }
}

function Read-Cvar([string]$Name) {
    $text = Invoke-Game @($Name) | Out-String
    $match = [regex]::Match($text, '=\s*"([^"]+)"')
    if (-not $match.Success) { throw "Could not read $Name from: $text" }
    return $match.Groups[1].Value
}

function Wait-ForMap {
    $deadline = (Get-Date).AddSeconds(15)
    do {
        Start-Sleep -Milliseconds 250
        try {
            $status = Invoke-Game @('users', 'status') | Out-String
            if ($status -match '(?m)^1 users\s*$' -and $status -match 'map\s+:\s+sp_a2_sphere_peek') {
                return
            }
        } catch {}
    } until ((Get-Date) -ge $deadline)
    throw 'sp_a2_sphere_peek did not become playable.'
}

function Stop-Inputs {
    Invoke-Game @('-forward', '-back', '-moveleft', '-moveright', '-jump', '-attack', '-attack2') | Out-Null
}

function Measure-Route([bool]$Patched, [int]$Run) {
    Invoke-Game @('map sp_a2_sphere_peek') | Out-Null
    Wait-ForMap
    Invoke-Game @(
        'sv_cheats 1',
        "fps_max $FpsMax",
        'give weapon_portalgun',
        'upgrade_portalgun',
        "p2boost_enable $([int]$Patched)",
        'p2boost_trace 1'
    ) | Out-Null

    $state = if ($Patched) { 'patched' } else { 'unpatched' }
    $variant = if ($Repeats -gt 1) { "$state-run$Run" } else { $state }
    $presentCsv = Join-Path $output "sphere-peek-$variant.csv"
    $metricsCsv = Join-Path $output "sphere-peek-$variant-metrics.csv"
    $workersCsv = Join-Path $output "sphere-peek-$variant-render-workers.csv"
    $profileCsv = Join-Path $output "sphere-peek-$variant-main-thread.csv"
    Remove-Item $presentCsv, $metricsCsv, $workersCsv, $profileCsv -Force -ErrorAction SilentlyContinue

    # Frame pacing is only representative while the game owns the foreground
    # window. Abort rather than accidentally measuring background throttling.
    & $focusGame -ProcessId $game.Id

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
	$threadCapture = if ($SampleMainThread -or $SampleThreadId) {
        if (-not (Test-Path -LiteralPath $profiler)) {
            throw 'p2profile.exe is missing; build the Release configuration first.'
        }

		# EIP sampling is research-only because it briefly suspends the selected
		# thread. Its QPC values align exactly with PresentMon's hitch window.
		$profileArguments = @(
			$game.Id,
			$DurationSeconds,
			$profileCsv
		)
		if ($SampleThreadId) {
			$profileArguments += @('--thread', $SampleThreadId)
		}
		if ($SampleStacks) {
			$profileArguments += '--stack'
		}
		Start-Process -FilePath $profiler -NoNewWindow -PassThru -ArgumentList $profileArguments
    } else {
        $null
    }

    try {
        # The route is ordinary collision-bound input: walk from spawn,
        # sidestep left around the panel, and enter the main chamber.
        Invoke-Game @(
			'wait_for 60 +forward; wait_for 240 -forward; wait_for 240 +moveleft; ' +
			'wait_for 300 -moveleft; wait_for 300 +forward; wait_for 780 -forward; ' +
            # Twelve ticks is the narrow line that clears the obstruction
            # without walking past the faith plate, then forward hits it.
			'wait_for 780 +moveleft; wait_for 792 -moveleft; wait_for 792 +forward; ' +
			'wait_for 1260 -forward'
        ) | Out-Null

        $capture.WaitForExit()
		if ($threadCapture) { $threadCapture.WaitForExit() }
        if (-not (Test-Path -LiteralPath $presentCsv) -or (Get-Item $presentCsv).Length -eq 0) {
            throw "PresentMon produced no frames for $variant."
        }
		if (($SampleMainThread -or $SampleThreadId) -and
            (-not (Test-Path -LiteralPath $profileCsv) -or (Get-Item $profileCsv).Length -eq 0)) {
            throw "p2profile produced no samples for $variant."
        }
    } finally {
        try { Stop-Inputs } catch {}
        if (-not $capture.HasExited) { $capture.Kill() }
		if ($threadCapture -and -not $threadCapture.HasExited) { $threadCapture.Kill() }
    }

    Invoke-Game @('p2boost_dump_metrics', 'p2boost_trace 0') | Out-Null
    Copy-Item (Join-Path $GameRoot 'p2boost_metrics.csv') $metricsCsv -Force
    $gameWorkers = Join-Path $GameRoot 'p2boost_metrics_render_workers.csv'
    if (Test-Path -LiteralPath $gameWorkers) {
        Copy-Item $gameWorkers $workersCsv -Force
    }
    Invoke-Game @('jpeg') | Out-Null
    Start-Sleep -Milliseconds 500
    $proof = Get-ChildItem (Join-Path $GameRoot 'portal2\screenshots') -Filter 'sp_a2_sphere_peek*.jpg' |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($proof) { Copy-Item $proof.FullName (Join-Path $output "sphere-peek-$variant-proof.jpg") -Force }

    & $analyze -InputCsv $presentCsv
    & $analyzeHitches -InputCsv $presentCsv -MetricsCsv $metricsCsv
}

$originalFpsMax = Read-Cvar 'fps_max'
$originalCheats = Read-Cvar 'sv_cheats'

try {
    for ($run = 1; $run -le $Repeats; ++$run) {
        if ($Variant -in @('Both', 'Unpatched')) { Measure-Route $false $run }
        if ($Variant -in @('Both', 'Patched')) { Measure-Route $true $run }
    }
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
}
