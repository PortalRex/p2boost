param(
	[string]$Demo = 'a/PortalGun_2243_76561198795823814_348449',
	[string]$OutputDirectory = 'artifacts\benchmarks\portalgun-vulkan',
	[ValidateRange(20, 40)][int]$DurationSeconds = 24,
	[ValidateSet(0, 360, 500, 999)][int]$FpsMax = 999,
	[ValidateSet('Patched', 'Unpatched')][string]$Variant = 'Patched',
	[string]$RconPassword = 'p2boostlab',
	[string]$GameRoot = 'D:\SteamLibrary\steamapps\common\Portal 2',
	[switch]$AlreadyForeground,
	[switch]$KeepGameOpen
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$rcon = Join-Path $PSScriptRoot 'invoke-rcon.ps1'
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

function Get-Percentile([double[]]$Values, [double]$Percentile) {
	$sorted = @($Values | Sort-Object)
	if($sorted.Count -eq 0) {
		return 0.0
	}
	$index = [Math]::Ceiling($Percentile * $sorted.Count) - 1
	return [double]$sorted[[Math]::Max(0, [Math]::Min($index, $sorted.Count - 1))]
}

$runtime = Invoke-Game @('p2boost_status') | Out-String
if($runtime -notmatch 'supported=1' -or $runtime -notmatch 'dxvk_vulkan=1') {
	throw "p2boost Vulkan runtime verification failed: $runtime"
}

$originalFpsMax = Read-Cvar 'fps_max'
$originalFrametimeUncap = Read-Cvar 'sar_frametime_uncap'
$presentCsv = Join-Path $output "portalgun-$slug.csv"
$metricsCsv = Join-Path $output "portalgun-$slug-metrics.csv"
$playbackLog = Join-Path $output "portalgun-$slug-playback.txt"
$markerFile = Join-Path $output "portalgun-$slug-marker.txt"
$capture = $null

Remove-Item $presentCsv, $metricsCsv, $playbackLog, $markerFile -Force -ErrorAction SilentlyContinue

try {
	Invoke-Game @(
		'stopdemo',
		"fps_max $FpsMax",
		"sar_frametime_uncap $([int]($FpsMax -eq 0))",
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

	Start-Sleep -Milliseconds 500
	$playbackQpc = [Diagnostics.Stopwatch]::GetTimestamp()
	$escapedDemo = $Demo.Replace('"', '')
	$playback = Invoke-Game @("playdemo `"$escapedDemo`"") 15 | Out-String
	$playback | Set-Content -LiteralPath $playbackLog
	$playbackQpc | Set-Content -LiteralPath $markerFile
	if($playback -notmatch 'Map:\s+sp_a1_intro3') {
		throw "Demo header did not report sp_a1_intro3: $playback"
	}

	$capture.WaitForExit()
	if(-not (Test-Path -LiteralPath $presentCsv) -or (Get-Item $presentCsv).Length -eq 0) {
		throw 'PresentMon produced no PortalGun demo frames.'
	}

	Invoke-Game @('p2boost_dump_metrics', 'p2boost_trace 0') 15 | Out-Null
	Copy-Item (Join-Path $GameRoot 'p2boost_metrics.csv') $metricsCsv -Force

	# The successful first portal occurs around 14 seconds of demo playback.
	# Report a broad 12-18 second window so small map-start variance cannot hide it.
	$frequency = [Diagnostics.Stopwatch]::Frequency
	$windowBegin = $playbackQpc + 12.0 * $frequency
	$windowEnd = $playbackQpc + 18.0 * $frequency
	$window = @(Import-Csv $presentCsv | Where-Object {
		[double]$_.CPUStartQPC -ge $windowBegin -and [double]$_.CPUStartQPC -le $windowEnd
	})
	if($window.Count -eq 0) {
		throw 'PresentMon contained no frames in the first-portal window.'
	}

	$frameTimes = @($window | ForEach-Object { [double]$_.FrameTime })
	$summary = [pscustomobject]@{
		Variant = $Variant
		Frames = $frameTimes.Count
		AverageFps = 1000.0 / (($frameTimes | Measure-Object -Average).Average)
		P99Ms = Get-Percentile $frameTimes 0.99
		MaximumMs = ($frameTimes | Measure-Object -Maximum).Maximum
		FramesAbove8_3Ms = @($frameTimes | Where-Object { $_ -ge 8.3 }).Count
	}
	$summary | Format-List
	$summary | Export-Csv (Join-Path $output "portalgun-$slug-summary.csv") -NoTypeInformation
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
			"sar_frametime_uncap $originalFrametimeUncap",
			"fps_max $originalFpsMax"
		) 3 | Out-Null
	}
	catch {}

	if(-not $KeepGameOpen) {
		& $stopGame -ProcessId $game.Id
	}
}

Write-Host "PortalGun Vulkan capture complete: $output"
