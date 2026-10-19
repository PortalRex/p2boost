param(
	# A project under research\ghidra\projects, e.g. current-client.
	[Parameter(Mandatory)][string]$Project,
	# The program inside it, e.g. client.dll.
	[Parameter(Mandatory)][string]$Program,
	[Parameter(Mandatory)][string]$Output,
	# RVAs relative to the image base; prefix with 'decompile' to also print C.
	[Parameter(Mandatory)][string[]]$Rva,
	# Ghidra refuses projects created by another OS user; name that user here.
	[string]$Owner = ''
)

# Resolves profile RVAs to containing functions (and optionally decompiles
# them) from an existing analyzed project, without re-running analysis.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$env:JAVA_HOME = (Get-ChildItem "$repo\.tools\jdk21" -Directory | Select-Object -First 1).FullName
if($Owner) {
	$env:_JAVA_OPTIONS = "-Duser.name=$Owner"
}
$headless = Join-Path (Get-ChildItem "$repo\.tools\ghidra" -Directory | Select-Object -First 1).FullName 'support\analyzeHeadless.bat'
$target = [IO.Path]::GetFullPath((Join-Path (Get-Location).Path $Output))
Remove-Item -LiteralPath $target -ErrorAction SilentlyContinue
# the JVM reports _JAVA_OPTIONS on stderr; the output file is the real result.
$ErrorActionPreference = 'Continue'
& $headless "$repo\research\ghidra\projects" $Project -process $Program -noanalysis -readOnly `
	-scriptPath "$repo\research\ghidra\scripts" -postScript DescribeRvas.java $target @Rva *> "$target.log"
if(-not (Test-Path -LiteralPath $target)) {
	throw "Ghidra produced no output; see $target.log"
}
