param(
	[int]$ProcessId = 0,
	[ValidateRange(1, 30)][int]$GraceSeconds = 5
)

$ErrorActionPreference = 'Stop'

$targets = if($ProcessId) {
	@(Get-Process -Id $ProcessId -ErrorAction SilentlyContinue)
} else {
	@(Get-Process portal2 -ErrorAction SilentlyContinue)
}

foreach($target in $targets) {
	if($target.ProcessName -ne 'portal2') {
		throw "Refusing to close non-Portal process $($target.ProcessName) ($($target.Id))."
	}

	# Prefer a normal shutdown so the engine can release its device and hooks.
	try {
		$target.CloseMainWindow() | Out-Null
		if($target.WaitForExit($GraceSeconds * 1000)) {
			continue
		}
	} catch {
		# A process that exited between enumeration and shutdown needs no work.
		if(-not (Get-Process -Id $target.Id -ErrorAction SilentlyContinue)) {
			continue
		}
	}

	# A hung benchmark must not leave the research DLL loaded in the game.
	Stop-Process -Id $target.Id -Force -ErrorAction SilentlyContinue
	$target.WaitForExit($GraceSeconds * 1000) | Out-Null
}

# Process exit can precede release of the mapped image by a few scheduler ticks.
$deadline = (Get-Date).AddSeconds($GraceSeconds)
do {
	$remaining = if($ProcessId) {
		Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
	} else {
		Get-Process portal2 -ErrorAction SilentlyContinue
	}
	if(-not $remaining) {
		break
	}
	Start-Sleep -Milliseconds 100
} while((Get-Date) -lt $deadline)

if($remaining) {
	throw 'Portal 2 did not exit after the benchmark.'
}
