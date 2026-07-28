param(
	[ValidateRange(1, 100)][int]$Cycles = 3,
	[string]$Output = 'artifacts\benchmarks\sar-compatibility.txt',
	[string]$RconPassword = 'p2boostlab',
	[switch]$KeepGameOpen
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$rcon = Join-Path $PSScriptRoot 'invoke-rcon.ps1'
$stopGame = Join-Path $PSScriptRoot 'stop-portal2.ps1'
$outputPath = [IO.Path]::GetFullPath((Join-Path $repo $Output))
$game = Get-Process portal2 -ErrorAction Stop | Select-Object -First 1

New-Item -ItemType Directory -Force (Split-Path $outputPath) | Out-Null
Set-Content -LiteralPath $outputPath -Value "p2boost / SAR lifecycle test, pid=$($game.Id), cycles=$Cycles"

function Invoke-Game([string[]]$Commands) {
	$response = & $rcon -ProcessId $game.Id -Password $RconPassword `
		-ResponseTimeoutSeconds 15 -Command $Commands | Out-String
	Add-Content -LiteralPath $outputPath -Value $response.TrimEnd()
	return $response
}

function Get-Plugins {
	$text = Invoke-Game @('plugin_print')
	$plugins = foreach($match in [regex]::Matches(
			$text,
			'(?m)^(\d+):\s+"([^"]+)"(?:\s+\((disabled)\))?\s*$')) {
		[pscustomobject]@{
			Index = [int]$match.Groups[1].Value
			Name = $match.Groups[2].Value
			Disabled = $match.Groups[3].Success
		}
	}
	return @($plugins)
}

function Find-Plugin([string]$Pattern) {
	$matches = @(Get-Plugins | Where-Object Name -Match $Pattern)
	if($matches.Count -gt 1) {
		throw "Multiple plugins matched '$Pattern'."
	}
	return $matches | Select-Object -First 1
}

function Enable-Plugin([string]$Pattern) {
	$plugin = Find-Plugin $Pattern
	if(-not $plugin) {
		throw "Plugin '$Pattern' is not loaded."
	}
	# Source disables a later entry when an earlier plugin is removed. Restore
	# that documented plugin state before evaluating the surviving DLL.
	if($plugin.Disabled) {
		Invoke-Game @("plugin_unpause $($plugin.Index)") | Out-Null
	}
}

function Unload-Plugin([string]$Pattern) {
	$plugin = Find-Plugin $Pattern
	if(-not $plugin) {
		return
	}
	Invoke-Game @("plugin_unload $($plugin.Index)") | Out-Null
	Start-Sleep -Milliseconds 250
	if(-not (Get-Process -Id $game.Id -ErrorAction SilentlyContinue)) {
		throw "Portal 2 exited while unloading '$($plugin.Name)'."
	}
	if(Find-Plugin $Pattern) {
		throw "Plugin '$($plugin.Name)' remained loaded."
	}
}

function Load-Plugin([string]$Filename, [string]$Pattern) {
	if(Find-Plugin $Pattern) {
		throw "Plugin '$Pattern' was already loaded."
	}
	Invoke-Game @("plugin_load $Filename") | Out-Null
	Start-Sleep -Milliseconds 250
	$plugin = Find-Plugin $Pattern
	if(-not $plugin -or $plugin.Disabled) {
		throw "Plugin '$Filename' did not load in an enabled state."
	}
}

function Assert-Compatible([ValidateSet('sar-first', 'p2boost-first')][string]$Order) {
	$sar = Find-Plugin '^SourceAutoRecord '
	$p2boost = Find-Plugin '^p2boost native-D3D9 '
	if(-not $sar -or -not $p2boost -or $sar.Disabled -or $p2boost.Disabled) {
		throw "Both plugins must be loaded and enabled for '$Order'."
	}
	if(($Order -eq 'sar-first' -and $sar.Index -ge $p2boost.Index) -or
		($Order -eq 'p2boost-first' -and $p2boost.Index -ge $sar.Index)) {
		throw "Plugin order did not become '$Order'."
	}

	$status = Invoke-Game @('p2boost_status')
	if($status -notmatch 'supported=1' -or $status -notmatch 'native_d3d9=1' -or
		$status -notmatch 'pacer_installed=1' -or $status -notmatch 'shadow_cache=1' -or
		$status -notmatch 'sar=1') {
		throw "p2boost validation failed in '$Order': $status"
	}
	$about = Invoke-Game @('sar_about')
	if($about -notmatch 'SourceAutoRecord' -or $about -notmatch 'Version:\s+\S+') {
		throw "SAR validation failed in '$Order': $about"
	}
}

try {
	for($cycle = 1; $cycle -le $Cycles; ++$cycle) {
		Add-Content -LiteralPath $outputPath -Value "[cycle $cycle p2boost-first]"

		# Remove only the two plugins under test. Any unrelated plugin remains
		# loaded and its displayed index is never assumed.
		Unload-Plugin '^p2boost native-D3D9 '
		Enable-Plugin '^SourceAutoRecord '
		Unload-Plugin '^SourceAutoRecord '
		Load-Plugin 'p2boost' '^p2boost native-D3D9 '
		Load-Plugin 'sar' '^SourceAutoRecord '
		Assert-Compatible 'p2boost-first'

		# Remove and restore the later owner while p2boost remains active.
		Unload-Plugin '^SourceAutoRecord '
		$status = Invoke-Game @('p2boost_status')
		if($status -notmatch 'sar=0' -or $status -notmatch 'pacer_installed=1') {
			throw "p2boost did not survive SAR unload: $status"
		}
		Load-Plugin 'sar' '^SourceAutoRecord '
		Assert-Compatible 'p2boost-first'

		Add-Content -LiteralPath $outputPath -Value "[cycle $cycle sar-first]"

		# Remove the earlier owner, reactivate the shifted SAR entry, then load
		# p2boost behind it. This exercises p2boost's full hook restoration.
		Unload-Plugin '^p2boost native-D3D9 '
		Enable-Plugin '^SourceAutoRecord '
		$about = Invoke-Game @('sar_about')
		if($about -notmatch 'SourceAutoRecord') {
			throw "SAR did not survive p2boost unload: $about"
		}
		Load-Plugin 'p2boost' '^p2boost native-D3D9 '
		Assert-Compatible 'sar-first'
	}
}
finally {
	if(-not $KeepGameOpen -and (Get-Process -Id $game.Id -ErrorAction SilentlyContinue)) {
		& $stopGame -ProcessId $game.Id
	}
}

Write-Host "SAR compatibility passed: cycles=$Cycles log=$outputPath"
