param(
	[string]$Demo = 'a/PortalGun_2243_76561198795823814_348449',
	[string]$OutputDirectory = 'artifacts\benchmarks\portalgun-demo',
	[ValidateRange(24, 40)][int]$DurationSeconds = 30,
	[ValidateSet(360, 500, 999)][int]$FpsMax = 999,
	[ValidateSet('Patched', 'Unpatched')][string]$Variant = 'Patched',
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
$enabled = [int]($Variant -eq 'Patched')
$slug = $Variant.ToLowerInvariant()

New-Item -ItemType Directory -Force $output | Out-Null

function Invoke-Game([string[]]$Commands, [int]$Timeout = 10) {
	for($attempt = 1; $attempt -le 3; ++$attempt) {
		try {
			return & $rcon -ProcessId $game.Id -Password $RconPassword `
				-ResponseTimeoutSeconds $Timeout -Command $Commands
		}
		catch {
			if($attempt -eq 3) {
				throw
			}

			Start-Sleep -Milliseconds 250
		}
	}
}

function Read-Cvar([string]$Name) {
	$text = Invoke-Game @($Name) | Out-String
	$match = [regex]::Match($text, '=\s*"([^"]+)"')
	if(-not $match.Success) {
		throw "Could not read $Name from: $text"
	}

	return $match.Groups[1].Value
}

$runtime = Invoke-Game @('p2boost_status') | Out-String
if($runtime -notmatch 'supported=1' -or $runtime -notmatch 'native_d3d9=1') {
	throw "p2boost runtime verification failed: $runtime"
}

$originalFpsMax = Read-Cvar 'fps_max'
$presentCsv = Join-Path $output "portalgun-$slug.csv"
$metricsCsv = Join-Path $output "portalgun-$slug-metrics.csv"
$workersCsv = Join-Path $output "portalgun-$slug-render-workers.csv"
$playbackLog = Join-Path $output "portalgun-$slug-playback.txt"
$capture = $null

Remove-Item $presentCsv, $metricsCsv, $workersCsv, $playbackLog -Force -ErrorAction SilentlyContinue

try {
	# Trace before loading the demo so the first portal-resource transition is
	# present even when it occurs immediately after intro3 becomes playable.
	Invoke-Game @(
		'stopdemo',
		"fps_max $FpsMax",
		"p2boost_enable $enabled",
		'p2boost_trace 1'
	) | Out-Null

	if(-not $AlreadyForeground) {
		& $focusGame -ProcessId $game.Id
	}
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

	# Give PresentMon time to establish its ETW session before playback starts.
	Start-Sleep -Milliseconds 500
	$escapedDemo = $Demo.Replace('"', '')
	$playback = Invoke-Game @("playdemo `"$escapedDemo`"") 15 | Out-String
	$playback | Set-Content -LiteralPath $playbackLog
	if($playback -notmatch 'Map:\s+sp_a1_intro3') {
		throw "Demo header did not report sp_a1_intro3: $playback"
	}

	$capture.WaitForExit()
	if(-not (Test-Path -LiteralPath $presentCsv) -or (Get-Item $presentCsv).Length -eq 0) {
		throw 'PresentMon produced no PortalGun demo frames.'
	}

	# Capture playback state before stopping it. This makes accidental early
	# termination or a wrong-map demo visible beside every benchmark artifact.
	$status = Invoke-Game @('status', 'demo_recordcommands') 5 | Out-String
	Add-Content -LiteralPath $playbackLog -Value $status
	Invoke-Game @('p2boost_dump_metrics', 'p2boost_trace 0') | Out-Null

	Copy-Item (Join-Path $GameRoot 'p2boost_metrics.csv') $metricsCsv -Force
	$gameWorkers = Join-Path $GameRoot 'p2boost_metrics_render_workers.csv'
	if(Test-Path -LiteralPath $gameWorkers) {
		Copy-Item $gameWorkers $workersCsv -Force
	}

	& $analyze -InputCsv $presentCsv
	& $analyzeHitches -InputCsv $presentCsv -MetricsCsv $metricsCsv
}
finally {
	if($capture -and -not $capture.HasExited) {
		$capture.Kill()
	}

	try {
		Invoke-Game @(
			'stopdemo',
			'p2boost_trace 0',
			'p2boost_enable 1',
			"fps_max $originalFpsMax"
		) 3 | Out-Null
	}
	catch {}

	if(-not $KeepGameOpen) {
		# Standalone benchmarks never leave research hooks loaded in the game.
		& $stopGame -ProcessId $game.Id
	}
}

Write-Host "PortalGun demo capture complete: $output"
