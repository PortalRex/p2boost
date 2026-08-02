param(
	[string]$Output = 'artifacts\etw\portalgun-fileio.etl',
	[string]$BenchmarkDirectory = 'artifacts\benchmarks\portalgun-fileio',
	[string]$GameRoot = 'D:\SteamLibrary\steamapps\common\Portal 2',
	[string]$Log = 'artifacts\etw\portalgun-fileio.log'
)

$ErrorActionPreference = 'Stop'

# Kernel FileIO events require an elevated token even when the desktop user's
# UAC notification level is disabled.
$principal = [Security.Principal.WindowsPrincipal]::new(
	[Security.Principal.WindowsIdentity]::GetCurrent())
if(-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
	throw 'The PortalGun FileIO capture must run from an elevated PowerShell process.'
}

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$runner = Join-Path $repo 'scripts\run-portalgun-demo.ps1'

# Resolve every artifact before starting WPR. The elevated child can otherwise
# inherit System32 as its working directory and write a valid trace there.
$target = if([IO.Path]::IsPathRooted($Output)) {
	[IO.Path]::GetFullPath($Output)
} else {
	[IO.Path]::GetFullPath((Join-Path $repo $Output))
}
$logTarget = if([IO.Path]::IsPathRooted($Log)) {
	[IO.Path]::GetFullPath($Log)
} else {
	[IO.Path]::GetFullPath((Join-Path $repo $Log))
}

New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
New-Item -ItemType Directory -Force (Split-Path $logTarget) | Out-Null
if(Test-Path -LiteralPath $target) {
	Remove-Item -LiteralPath $target -Force
}
if(Test-Path -LiteralPath $logTarget) {
	Remove-Item -LiteralPath $logTarget -Force
}

function Invoke-Wpr([string]$Arguments) {
	# Start-Process keeps WPR's expected "no active session" error stream from
	# becoming a terminating PowerShell NativeCommandError.
	$process = Start-Process -FilePath (Join-Path $env:SystemRoot 'System32\wpr.exe') `
		-ArgumentList $Arguments -Wait -PassThru -NoNewWindow
	return $process.ExitCode
}

$started = $false
$transcribing = $false
try {
	Start-Transcript -LiteralPath $logTarget -Force | Out-Null
	$transcribing = $true

	# Canceling a nonexistent session is harmless and prevents an interrupted
	# earlier capture from producing an empty or mixed ETL.
	Invoke-Wpr '-cancel' | Out-Null
	if((Invoke-Wpr '-start FileIO -filemode') -ne 0) {
		throw 'WPR could not start the FileIO profile.'
	}
	$started = $true

	& $runner -OutputDirectory $BenchmarkDirectory -DurationSeconds 24 `
		-Variant Patched -AlreadyForeground -GameRoot $GameRoot

	if((Invoke-Wpr "-stop `"$target`"") -ne 0) {
		throw 'WPR could not finalize the FileIO trace.'
	}
	$started = $false

	$trace = Get-Item -LiteralPath $target
	if($trace.Length -eq 0) {
		throw "WPR produced an empty trace: $target"
	}
	Write-Host "PortalGun FileIO capture complete: $target"
	Write-Host ("ETL size: {0:N1} MiB" -f ($trace.Length / 1MB))
}
finally {
	if($started) {
		Invoke-Wpr '-cancel' | Out-Null
	}
	if($transcribing) {
		Stop-Transcript | Out-Null
	}
}
