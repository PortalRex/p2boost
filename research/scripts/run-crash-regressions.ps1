param(
	[ValidateRange(1, 100)][int]$GooCycles = 20,
	[int]$ProcessId = 0,
	[string]$RconPassword = 'p2boostlab',
	[string]$Output = 'artifacts\benchmarks\crash-regressions.txt',
	[string]$DumpDirectory = '',
	[switch]$KeepOverlayEnabled
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$rcon = Join-Path $repo 'scripts\invoke-rcon.ps1'
$outputPath = [IO.Path]::GetFullPath((Join-Path $repo $Output))
$game = if($ProcessId) { Get-Process -Id $ProcessId -ErrorAction Stop } else {
	Get-Process portal2 -ErrorAction Stop | Select-Object -First 1
}

if(-not $DumpDirectory) {
	$steamPath = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
	if($steamPath) { $DumpDirectory = Join-Path $steamPath 'dumps' }
}

$started = Get-Date
New-Item -ItemType Directory -Force (Split-Path $outputPath) | Out-Null
Set-Content -LiteralPath $outputPath -Value "p2boost crash regressions, pid=$($game.Id), started=$($started.ToString('o'))"

function Assert-GameAlive {
	if(-not (Get-Process -Id $game.Id -ErrorAction SilentlyContinue)) {
		throw 'Portal 2 exited during the crash regression.'
	}
}

function Invoke-Game([string[]]$Commands, [int]$Timeout = 10) {
	for($attempt = 1; $attempt -le 4; ++$attempt) {
		try {
			return & $rcon -ProcessId $game.Id -Password $RconPassword `
				-ResponseTimeoutSeconds $Timeout -Command $Commands
		}
		catch {
			Assert-GameAlive
			if($attempt -eq 4) { throw }
			Start-Sleep -Milliseconds 250
		}
	}
}

function Wait-ForMap([string]$Map) {
	$deadline = (Get-Date).AddSeconds(30)
	do {
		Assert-GameAlive
		Start-Sleep -Milliseconds 250
		try {
			$status = Invoke-Game @('status') 3 | Out-String
			if($status -match ("map\s+:\s+" + [regex]::Escape($Map))) { return }
		}
		catch {
			# RCON is expected to disappear briefly while the client loads a map.
		}
	} while((Get-Date) -lt $deadline)
	throw "Portal 2 did not finish loading $Map."
}

function Assert-NoNewDump {
	if(-not $DumpDirectory -or -not (Test-Path -LiteralPath $DumpDirectory)) { return }
	$new = @(Get-ChildItem -LiteralPath $DumpDirectory -Filter '*.dmp' -ErrorAction SilentlyContinue |
		Where-Object LastWriteTime -ge $started)
	if($new.Count) {
		throw "Steam wrote a new crash dump: $($new[0].FullName)"
	}
}

$runtime = Invoke-Game @('p2boost_status') | Out-String
if($runtime -notmatch 'supported=1' -or $runtime -notmatch 'dxvk_vulkan=1' -or
	$runtime -notmatch 'shadow_cache=1' -or $runtime -notmatch 'flat_available=1' -or
	$runtime -notmatch 'overlay_available=1') {
	throw "p2boost runtime verification failed: $runtime"
}
Add-Content -LiteralPath $outputPath -Value $runtime.TrimEnd()

# This used to fault immediately because GetMaxClients was called through the
# adjacent Con_NPrintf slot. Exercise it before loading the goo map as well as
# throughout every water-volume query below.
Invoke-Game @('p2boost_overlay 1') | Out-Null
Start-Sleep -Milliseconds 500
Assert-GameAlive
$runtime = Invoke-Game @('p2boost_status') | Out-String
if($runtime -notmatch 'overlay=1') { throw "Overlay did not remain enabled: $runtime" }
Assert-NoNewDump

try {
	try { Invoke-Game @('map sp_a2_laser_over_goo') 3 | Out-Null } catch { Assert-GameAlive }
	Wait-ForMap 'sp_a2_laser_over_goo'
	Invoke-Game @('sv_cheats 1', 'god 0', 'buddha 0', 'p2boost_enable 1') | Out-Null

	for($cycle = 1; $cycle -le $GooCycles; ++$cycle) {
		Invoke-Game @('setpos 3232 -1280 -120') | Out-Null
		Start-Sleep -Milliseconds 1750
		Assert-GameAlive
		Assert-NoNewDump
		$status = Invoke-Game @('status', 'p2boost_status') | Out-String
		if($status -notmatch 'map\s+:\s+sp_a2_laser_over_goo' -or $status -notmatch 'overlay=1') {
			throw "Runtime state changed after goo cycle $cycle`: $status"
		}
		Add-Content -LiteralPath $outputPath -Value "goo_cycle=$cycle ok"
	}
}
finally {
	if(Get-Process -Id $game.Id -ErrorAction SilentlyContinue) {
		if(-not $KeepOverlayEnabled) {
			try { Invoke-Game @('p2boost_overlay 0') 3 | Out-Null } catch {}
		}
	}
}

Assert-NoNewDump
Add-Content -LiteralPath $outputPath -Value "PASS goo_cycles=$GooCycles overlay=1 new_dumps=0"
Write-Host "Crash regressions passed: goo_cycles=$GooCycles log=$outputPath"
