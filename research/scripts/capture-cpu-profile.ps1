param(
	[Parameter(Mandatory)][string]$Output,
	[ValidateRange(1, 60)][int]$Seconds = 6,
	# 1221 x 100 ns is the shortest interval WPR accepts (~8 kHz).
	[ValidateRange(1221, 100000)][int]$ProfileInterval = 1221
)
$ErrorActionPreference = 'Stop'

# Kernel sampling needs elevation. This host elevates without a consent
# prompt, so the capture runs in a hidden elevated child and is then dumped to
# text for research\tools\etw_profile.py.
$target = [IO.Path]::GetFullPath($Output)
New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
$child = Join-Path $env:TEMP "p2boost-wpr-$PID.ps1"
@"
`$wpr = "`$env:SystemRoot\System32\wpr.exe"
& `$wpr -cancel 2>`$null | Out-Null
& `$wpr -setprofint $ProfileInterval | Out-Null
& `$wpr -start CPU -filemode | Out-Null
Start-Sleep -Seconds $Seconds
& `$wpr -stop "$target" | Out-Null
"@ | Set-Content -LiteralPath $child
try {
	Start-Process powershell -Verb RunAs -WindowStyle Hidden -Wait -ArgumentList @(
		'-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $child)
}
finally {
	Remove-Item -LiteralPath $child -ErrorAction SilentlyContinue
}
if(-not (Test-Path -LiteralPath $target)) {
	throw "WPR produced no trace: $target"
}
$xperf = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Windows Performance Toolkit\xperf.exe'
# xperf reports progress on stderr, which Windows PowerShell treats as an error.
$dumper = Start-Process -FilePath $xperf -WindowStyle Hidden -Wait -PassThru `
	-ArgumentList @('-i', "`"$target`"", '-o', "`"$target.txt`"", '-a', 'dumper')
if($dumper.ExitCode -ne 0) {
	throw "xperf exited with $($dumper.ExitCode)"
}
Write-Host "$target.txt"
