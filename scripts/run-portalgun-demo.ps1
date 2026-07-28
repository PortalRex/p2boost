param(
	[string]$Demo = 'a/PortalGun_2243_76561198795823814_348449',
	[string]$OutputDirectory = 'artifacts\benchmarks\portalgun-demo',
	[ValidateRange(24, 40)][int]$DurationSeconds = 30,
	[ValidateSet(360, 500, 999)][int]$FpsMax = 999,
	[ValidateSet('Patched', 'Unpatched')][string]$Variant = 'Patched',
	[string]$RconPassword = 'p2boostlab',
	[string]$GameRoot = 'D:\SteamLibrary\steamapps\common\Portal 2',
	[switch]$SampleAdaptive,
	[switch]$SampleMainThread,
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
$profiler = Join-Path $repo 'build\vs2022-x86\Release\p2profile.exe'
$output = [IO.Path]::GetFullPath((Join-Path $repo $OutputDirectory))
$game = Get-Process portal2 -ErrorAction Stop | Select-Object -First 1
if($SampleAdaptive -and $SampleMainThread) {
	throw 'Choose either adaptive or main-thread sampling, not both.'
}
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
$modulesCsv = Join-Path $output "portalgun-$slug-modules.csv"
$sampleKind = if($SampleAdaptive) { 'adaptive' } else { 'main-thread' }
$sampleCsv = Join-Path $output "portalgun-$slug-$sampleKind.csv"
$capture = $null
$sampleCapture = $null

Remove-Item $presentCsv, $metricsCsv, $workersCsv, $playbackLog, $modulesCsv, $sampleCsv -Force -ErrorAction SilentlyContinue

try {
	# Preserve the 32-bit ASLR layout so every captured return address can be
	# reduced to a stable module RVA after the benchmark process has exited.
	# A 64-bit PowerShell process sees only the WOW64 host modules here, so use
	# the bundled x86 profiler's Toolhelp snapshot.
	if(-not (Test-Path -LiteralPath $profiler)) {
		throw "p2profile.exe is missing: $profiler"
	}
	& $profiler $game.Id --modules | Set-Content -LiteralPath $modulesCsv
	if($LASTEXITCODE -ne 0 -or (Get-Item -LiteralPath $modulesCsv).Length -eq 0) {
		throw 'p2profile did not produce a valid module map.'
	}

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
	if($SampleAdaptive -or $SampleMainThread) {
		if(-not (Test-Path -LiteralPath $profiler)) {
			throw "p2profile.exe is missing: $profiler"
		}
		# Both sampler modes are low-overhead and use QPC timestamps directly
		# comparable to PresentMon and the plugin's frame metrics.
		$sampleArguments = @($game.Id, $DurationSeconds, $sampleCsv)
		if($SampleAdaptive) {
			$sampleArguments += '--adaptive'
		}
		$sampleCapture = Start-Process -FilePath $profiler -NoNewWindow -PassThru -ArgumentList $sampleArguments
	}

	# Give PresentMon time to establish its ETW session before playback starts.
	Start-Sleep -Milliseconds 500
	$escapedDemo = $Demo.Replace('"', '')
	$playback = Invoke-Game @("playdemo `"$escapedDemo`"") 15 | Out-String
	$playback | Set-Content -LiteralPath $playbackLog
	if($playback -notmatch 'Map:\s+sp_a1_intro3') {
		throw "Demo header did not report sp_a1_intro3: $playback"
	}

	$capture.WaitForExit()
	if($sampleCapture) {
		$sampleCapture.WaitForExit()
		$sampleExitCode = $sampleCapture.ExitCode
		if(($null -ne $sampleExitCode -and $sampleExitCode -ne 0) -or
			-not (Test-Path -LiteralPath $sampleCsv) -or (Get-Item -LiteralPath $sampleCsv).Length -eq 0) {
			throw 'p2profile did not produce a valid sample capture.'
		}
	}
	if(-not (Test-Path -LiteralPath $presentCsv) -or (Get-Item $presentCsv).Length -eq 0) {
		throw 'PresentMon produced no PortalGun demo frames.'
	}

	# Capture playback state before stopping it. This makes accidental early
	# termination or a wrong-map demo visible beside every benchmark artifact.
	$status = Invoke-Game @('status', 'demo_recordcommands') 5 | Out-String
	Add-Content -LiteralPath $playbackLog -Value $status
	# Research builds write a much wider worker CSV than production. Give the
	# synchronous dump enough time so RCON does not retry the same command.
	Invoke-Game @('p2boost_dump_metrics', 'p2boost_trace 0') 45 | Out-Null

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
	if($sampleCapture -and -not $sampleCapture.HasExited) {
		$sampleCapture.Kill()
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
