param(
	[string]$GameRoot = 'D:\SteamLibrary\steamapps\common\Portal 2',
	# Binaries relative to the game root.
	[string[]]$Binary = @('portal2\bin\client.dll', 'bin\engine.dll', 'bin\materialsystem.dll', 'bin\shaderapivk.dll',
		'bin\vguimatsurface.dll', 'bin\inputsystem.dll')
)

# Imports each game binary into research\ghidra\projects\current-<name> (full
# auto-analysis, which takes minutes per DLL) and exports its function ranges
# to artifacts\ghidra\functions\<name>.csv for research\tools\etw_profile.py
# --functions. research\scripts\ghidra-describe.ps1 then reads these projects.
# Requires scripts\bootstrap-tools.ps1.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$env:JAVA_HOME = (Get-ChildItem "$repo\.tools\jdk21" -Directory | Select-Object -First 1).FullName
$headless = Join-Path (Get-ChildItem "$repo\.tools\ghidra" -Directory | Select-Object -First 1).FullName 'support\analyzeHeadless.bat'
$projects = "$repo\research\ghidra\projects"
$functions = "$repo\artifacts\ghidra\functions"
New-Item -ItemType Directory -Force $projects, $functions | Out-Null

foreach($relative in $Binary) {
	$path = Join-Path $GameRoot $relative
	if(-not (Test-Path -LiteralPath $path)) {
		throw "missing game binary: $path"
	}
	$name = [IO.Path]::GetFileName($path)
	$project = 'current-' + [IO.Path]::GetFileNameWithoutExtension($path)
	$ErrorActionPreference = 'Continue'
	& $headless $projects $project -import $path -overwrite -scriptPath "$repo\research\ghidra\scripts" `
		-postScript ExportFunctionRanges.java "$functions\$name.csv" -analysisTimeoutPerFile 1800 *> "$functions\$name.log"
	$ErrorActionPreference = 'Stop'
	if(-not (Test-Path -LiteralPath "$functions\$name.csv")) {
		throw "Ghidra failed for $name; see $functions\$name.log"
	}
	Write-Host "$name -> $project, $functions\$name.csv"
}
