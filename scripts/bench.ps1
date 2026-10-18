param(
	# Each variant is "name=command;command". Commands run before every capture
	# of that variant, so a variant must fully describe its state (for example
	# both "r_drawvgui 0" and "r_drawvgui 1" variants, not only the change).
	[string[]]$Variant = @('stock='),
	[ValidateRange(1, 50)][int]$Rounds = 5,
	[ValidateRange(3, 120)][int]$Seconds = 10,
	[ValidateRange(0, 60)][double]$SettleSeconds = 2,
	[ValidateRange(0, 120)][int]$WarmupSeconds = 10,
	# After the measured rounds, record an elevated ETW CPU profile of this
	# length for every variant (see research\tools\etw_profile.py).
	[ValidateRange(0, 60)][int]$ProfileSeconds = 0,
	[string]$Map = 'mp_coop_lobby_3',
	[switch]$SinglePlayer,
	[int]$FpsMax = 0,
	# Optional p2boost build to install for this run. The installed DLL is
	# restored afterwards.
	[string]$Plugin = '',
	[string]$GameRoot = 'D:\SteamLibrary\steamapps\common\Portal 2',
	[string]$OutputDirectory = '',
	[int]$NetconPort = 2121,
	[string[]]$LaunchOption = @('-novid', '-high', '-vulkan'),
	[switch]$KeepGameOpen
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$focusGame = Join-Path $PSScriptRoot 'set-game-foreground.ps1'
$stopGame = Join-Path $PSScriptRoot 'stop-portal2.ps1'
$presentMon = Join-Path $repo '.tools\downloads\PresentMon.exe'
if(-not (Test-Path -LiteralPath $presentMon)) {
	throw 'PresentMon is missing; run research\scripts\bootstrap-tools.ps1 first.'
}
if(-not $OutputDirectory) {
	$OutputDirectory = Join-Path $repo ('artifacts\bench\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$output = if([IO.Path]::IsPathRooted($OutputDirectory)) {
	[IO.Path]::GetFullPath($OutputDirectory)
} else {
	[IO.Path]::GetFullPath((Join-Path $repo $OutputDirectory))
}
New-Item -ItemType Directory -Force $output | Out-Null

$variants = foreach($spec in $Variant) {
	$separator = $spec.IndexOf('=')
	if($separator -lt 1) {
		throw "Variant '$spec' must be name=command;command"
	}
	[pscustomobject]@{
		Name     = $spec.Substring(0, $separator)
		Commands = @($spec.Substring($separator + 1).Split(';') | ForEach-Object { $_.Trim() } | Where-Object { $_ })
	}
}
if(@($variants.Name | Select-Object -Unique).Count -ne $variants.Count) {
	throw 'Variant names must be unique.'
}

# The net console accepts commands from the main menu, unlike RCON, which only
# listens while a server is running. A random password keeps the listener
# private for this run.
$password = [Guid]::NewGuid().ToString('N')
$script:client = $null
$script:stream = $null
$script:console = [Text.StringBuilder]::new()
$script:marker = 0

function Read-Console {
	$buffer = New-Object byte[] 65536
	while($script:stream.DataAvailable) {
		$count = $script:stream.Read($buffer, 0, $buffer.Length)
		if($count -le 0) {
			break
		}
		[void]$script:console.Append([Text.Encoding]::ASCII.GetString($buffer, 0, $count))
	}
}

# Sends commands followed by a unique echo and returns the console text printed
# until that echo arrives, so each call observes exactly its own output.
function Invoke-Game([string[]]$Commands, [int]$TimeoutSeconds = 10) {
	++$script:marker
	$token = "p2bench_mark_$script:marker"
	Read-Console
	$start = $script:console.Length
	foreach($command in @($Commands) + "echo $token") {
		$bytes = [Text.Encoding]::ASCII.GetBytes("$command`n")
		$script:stream.Write($bytes, 0, $bytes.Length)
	}
	$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
	while((Get-Date) -lt $deadline) {
		Read-Console
		$text = $script:console.ToString($start, $script:console.Length - $start)
		$index = $text.IndexOf($token)
		if($index -ge 0) {
			return $text.Substring(0, $index)
		}
		[Threading.Thread]::Sleep(20)
	}
	throw "Timed out waiting for the game to run: $($Commands -join '; ')"
}

function Read-Cvar([string]$Name) {
	$text = Invoke-Game @($Name)
	$match = [regex]::Match($text, '"' + [regex]::Escape($Name) + '" = "([^"]*)"')
	if(-not $match.Success) {
		throw "Could not read $Name from: $text"
	}
	return $match.Groups[1].Value
}

function Get-Percentile([double[]]$Sorted, [double]$Fraction) {
	$index = [Math]::Ceiling($Fraction * $Sorted.Count) - 1
	return $Sorted[[Math]::Max(0, [Math]::Min($Sorted.Count - 1, $index))]
}

function Measure-Capture([string]$Path) {
	$rows = @(Import-Csv -LiteralPath $Path)
	# PresentMon cannot time the first present it observes on a swap chain.
	$rows = @($rows | Group-Object SwapChainAddress | ForEach-Object { $_.Group | Select-Object -Skip 1 })
	# Tearing down the trace session stalls the game for up to ~120 ms about two
	# seconds before PresentMon exits, so the capture runs longer than measured
	# and only the first $Seconds of frames count.
	if($rows.Count) {
		$end = [double]$rows[0].CPUStartQPC + $Seconds * [Diagnostics.Stopwatch]::Frequency
		$rows = @($rows | Where-Object { [double]$_.CPUStartQPC -lt $end })
	}
	# --v2_metrics names the present-to-present interval FrameTime.
	$frames = @($rows | ForEach-Object { [double]$_.FrameTime } | Where-Object { $_ -gt 0 })
	if($frames.Count -lt 100) {
		throw "capture $Path contains only $($frames.Count) frames"
	}
	$sorted = [double[]]@($frames | Sort-Object)
	$total = ($frames | Measure-Object -Sum).Sum
	$cpu = [double[]]@($rows | ForEach-Object { [double]$_.CPUBusy } | Sort-Object)
	$gpu = [double[]]@($rows | Where-Object { $_.GPUBusy -ne 'NA' } | ForEach-Object { [double]$_.GPUBusy } | Sort-Object)
	# 1% low is the average rate of the slowest 1% of frames, which is less
	# noisy than the single p99 sample.
	$slowest = @($sorted | Select-Object -Last ([Math]::Max(1, [int]($sorted.Count / 100))))
	[pscustomobject]@{
		Frames     = $frames.Count
		AverageFps = 1000.0 * $frames.Count / $total
		MedianMs   = Get-Percentile $sorted 0.5
		P99Ms      = Get-Percentile $sorted 0.99
		Low1Fps    = 1000.0 * $slowest.Count / ($slowest | Measure-Object -Sum).Sum
		CpuBusyMs  = Get-Percentile $cpu 0.5
		GpuBusyMs  = if($gpu.Count) { Get-Percentile $gpu 0.5 } else { [double]::NaN }
	}
}

function Get-Interval([double[]]$Values) {
	$count = $Values.Count
	$mean = ($Values | Measure-Object -Average).Average
	if($count -lt 2) {
		return [pscustomobject]@{ Mean = $mean; HalfWidth = [double]::NaN }
	}
	$variance = ($Values | ForEach-Object { ($_ - $mean) * ($_ - $mean) } | Measure-Object -Sum).Sum / ($count - 1)
	# Two-sided 95% Student t quantiles for small round counts.
	$t = @{ 1 = 12.706; 2 = 4.303; 3 = 3.182; 4 = 2.776; 5 = 2.571; 6 = 2.447; 7 = 2.365; 8 = 2.306; 9 = 2.262 }
	$quantile = if($t.ContainsKey($count - 1)) { $t[$count - 1] } else { 2.0 }
	[pscustomobject]@{ Mean = $mean; HalfWidth = $quantile * [Math]::Sqrt($variance / $count) }
}

# extra recording discarded by Measure-Capture; see the teardown note there.
$captureTail = 3
$pluginTarget = Join-Path $GameRoot 'p2boost.dll'
$pluginBackup = Join-Path $output 'p2boost.installed.dll'
$swappedPlugin = $false
$game = $null
$results = [Collections.Generic.List[object]]::new()

try {
	if(Get-Process portal2 -ErrorAction SilentlyContinue) {
		throw 'Portal 2 is already running; close it so the benchmark owns a fresh process.'
	}
	if($Plugin) {
		Copy-Item -LiteralPath $pluginTarget -Destination $pluginBackup -Force
		Copy-Item -LiteralPath $Plugin -Destination $pluginTarget -Force
		$swappedPlugin = $true
	}
	$pluginHash = (Get-FileHash -LiteralPath $pluginTarget -Algorithm SHA256).Hash

	$arguments = @($LaunchOption) + @('-netconport', $NetconPort, '-netconpassword', $password)
	$game = Start-Process -FilePath (Join-Path $GameRoot 'portal2.exe') -WorkingDirectory $GameRoot `
		-ArgumentList $arguments -PassThru
	$deadline = (Get-Date).AddSeconds(120)
	while(-not $script:client) {
		if($game.HasExited) {
			throw 'Portal 2 exited during startup.'
		}
		if((Get-Date) -gt $deadline) {
			throw 'The net console never started listening.'
		}
		try {
			$script:client = [Net.Sockets.TcpClient]::new('127.0.0.1', $NetconPort)
		}
		catch {
			[Threading.Thread]::Sleep(1000)
		}
	}
	$script:stream = $script:client.GetStream()
	$bytes = [Text.Encoding]::ASCII.GetBytes("PASS $password`n")
	$script:stream.Write($bytes, 0, $bytes.Length)
	# The engine finishes executing startup configs a few frames after the
	# listener opens; the echo round-trip proves commands are accepted.
	Invoke-Game @('echo ready') 120 | Out-Null

	$loadCommand = if($SinglePlayer) { "map $Map" } else { "ss_map $Map" }
	Invoke-Game @($loadCommand) 120 | Out-Null
	$deadline = (Get-Date).AddSeconds(120)
	do {
		[Threading.Thread]::Sleep(1000)
		$status = Invoke-Game @('status') 30
	} until(($status -match "map\s+:\s+$Map\b") -or (Get-Date) -gt $deadline)
	if($status -notmatch "map\s+:\s+$Map\b") {
		throw "Map $Map did not load."
	}

	$uncap = if($FpsMax -eq 0) { 1 } else { 0 }
	Invoke-Game @("fps_max $FpsMax", "sar_frametime_uncap $uncap") | Out-Null
	$build = (Invoke-Game @('p2boost_status')).Trim()
	$build | Set-Content -LiteralPath (Join-Path $output 'p2boost-status.txt')

	& $focusGame -ProcessId $game.Id
	if($WarmupSeconds) {
		Invoke-Game $variants[0].Commands | Out-Null
		[Threading.Thread]::Sleep($WarmupSeconds * 1000)
	}

	for($round = 1; $round -le $Rounds; ++$round) {
		# Alternate the order every round (ABBA...) so slow drift such as heat
		# or background activity cannot favor one variant.
		$order = if($round % 2) { $variants } else { $variants[($variants.Count - 1)..0] }
		foreach($entry in $order) {
			Invoke-Game $entry.Commands | Out-Null
			& $focusGame -ProcessId $game.Id
			[Threading.Thread]::Sleep([int]($SettleSeconds * 1000))
			$capture = Join-Path $output ('{0}-r{1}.csv' -f $entry.Name, $round)
			# PresentMon warns on stderr when unelevated; Windows PowerShell would
			# turn that into a terminating error if it were invoked directly.
			$recorder = Start-Process -FilePath $presentMon -WindowStyle Hidden -Wait -PassThru -ArgumentList @(
				'--process_id', $game.Id, '--output_file', "`"$capture`"", '--v2_metrics', '--qpc_time',
				'--stop_existing_session', '--timed', ($Seconds + $captureTail), '--terminate_after_timed', '--no_console_stats')
			if($recorder.ExitCode -ne 0) {
				throw "PresentMon exited with $($recorder.ExitCode)"
			}
			$metrics = Measure-Capture $capture
			$metrics | Add-Member Variant $entry.Name
			$metrics | Add-Member Round $round
			$results.Add($metrics)
			Write-Host ('round {0} {1,-16} {2,8:N1} fps  median {3:N3} ms  1% low {4:N1}' -f
				$round, $entry.Name, $metrics.AverageFps, $metrics.MedianMs, $metrics.Low1Fps)
		}
	}

	if($ProfileSeconds) {
		$profiler = Join-Path $repo 'research\scripts\capture-cpu-profile.ps1'
		foreach($entry in $variants) {
			Invoke-Game $entry.Commands | Out-Null
			& $focusGame -ProcessId $game.Id
			[Threading.Thread]::Sleep([int]($SettleSeconds * 1000))
			& $profiler -Output (Join-Path $output "$($entry.Name).etl") -Seconds $ProfileSeconds | Out-Null
		}
	}
}
finally {
	if($script:client) {
		$script:client.Dispose()
	}
	if($game -and -not $KeepGameOpen) {
		& $stopGame -ProcessId $game.Id
	}
	if($swappedPlugin -and -not $KeepGameOpen) {
		Copy-Item -LiteralPath $pluginBackup -Destination $pluginTarget -Force
	}
}

$results | Select-Object Variant, Round, Frames, AverageFps, MedianMs, P99Ms, Low1Fps, CpuBusyMs, GpuBusyMs |
	Export-Csv -NoTypeInformation -LiteralPath (Join-Path $output 'results.csv')

$baseline = $variants[0].Name
$summary = [Text.StringBuilder]::new()
[void]$summary.AppendLine("# Benchmark $(Split-Path -Leaf $output)")
[void]$summary.AppendLine()
[void]$summary.AppendLine("Map ``$Map`` ($(if($SinglePlayer) { 'map' } else { 'ss_map' })), fps_max $FpsMax, $Rounds rounds x $Seconds s, plugin SHA-256 ``$pluginHash``.")
[void]$summary.AppendLine()
[void]$summary.AppendLine('| Variant | Avg FPS (95% CI) | vs baseline (paired 95% CI) | Median ms | 1% low FPS | CPU busy ms | GPU busy ms |')
[void]$summary.AppendLine('|---|---:|---:|---:|---:|---:|---:|')
foreach($entry in $variants) {
	$rows = @($results | Where-Object Variant -eq $entry.Name | Sort-Object Round)
	$fps = Get-Interval ([double[]]@($rows.AverageFps))
	$delta = '-'
	if($entry.Name -ne $baseline) {
		$base = @($results | Where-Object Variant -eq $baseline | Sort-Object Round)
		# Pair each round with the baseline from the same round to cancel drift.
		$ratios = [double[]]@(for($i = 0; $i -lt $rows.Count; ++$i) { 100.0 * ($rows[$i].AverageFps / $base[$i].AverageFps - 1.0) })
		$change = Get-Interval $ratios
		$delta = '{0:+0.0;-0.0}% +/- {1:N1}' -f $change.Mean, $change.HalfWidth
	}
	[void]$summary.AppendLine(('| {0} | {1:N1} +/- {2:N1} | {3} | {4:N3} | {5:N1} | {6:N3} | {7:N3} |' -f
		$entry.Name, $fps.Mean, $fps.HalfWidth, $delta,
		($rows.MedianMs | Measure-Object -Average).Average,
		($rows.Low1Fps | Measure-Object -Average).Average,
		($rows.CpuBusyMs | Measure-Object -Average).Average,
		($rows.GpuBusyMs | Measure-Object -Average).Average))
}
[void]$summary.AppendLine()
foreach($entry in $variants) {
	[void]$summary.AppendLine("- ``$($entry.Name)``: ``$($entry.Commands -join '; ')``")
}
$summary.ToString() | Set-Content -LiteralPath (Join-Path $output 'summary.md')
Write-Host
Write-Host $summary.ToString()
Write-Host "Results: $output"
