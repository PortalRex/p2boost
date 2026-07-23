param(
    [string]$GameRoot = 'D:\SteamLibrary\steamapps\common\Portal 2',
    [string]$GhidraRoot = '',
    [string]$JavaHome = '',
    [ValidateSet('all','engine','shaderapidx9','materialsystem','studiorender','stdshader_dx9','filesystem_stdio','soundemittersystem','client','server')]
    [string[]]$Module = @('all'),
    [switch]$Fresh
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $GhidraRoot) {
    $GhidraRoot = (Get-ChildItem "$repo\.tools\ghidra" -Directory | Select-Object -First 1).FullName
}
if (-not $JavaHome) {
    $JavaHome = (Get-ChildItem "$repo\.tools\jdk21" -Directory | Select-Object -First 1).FullName
}
$headless = Join-Path $GhidraRoot 'support\analyzeHeadless.bat'
if (-not (Test-Path -LiteralPath $headless)) { throw "analyzeHeadless not found: $headless" }

$projectRoot = Join-Path $repo 'research\ghidra\projects'
$exportRoot = Join-Path $repo 'artifacts\ghidra'
$scriptRoot = Join-Path $repo 'research\ghidra\scripts'
New-Item -ItemType Directory -Force $projectRoot,$exportRoot | Out-Null
$env:JAVA_HOME = $JavaHome

$binaries = @(
    'bin\engine.dll',
    'bin\shaderapidx9.dll',
    'bin\materialsystem.dll',
    'bin\studiorender.dll',
    'bin\stdshader_dx9.dll',
    'bin\filesystem_stdio.dll',
    'bin\soundemittersystem.dll',
    'portal2\bin\client.dll',
    'portal2\bin\server.dll'
)
if ($Module -notcontains 'all') {
    $selected = @($Module | ForEach-Object { $_.ToLowerInvariant() + '.dll' })
    $binaries = @($binaries | Where-Object { $selected -contains [IO.Path]::GetFileName($_).ToLowerInvariant() })
}
foreach ($relative in $binaries) {
    $binary = Join-Path $GameRoot $relative
    if (-not (Test-Path -LiteralPath $binary)) { throw "missing game binary: $binary" }
    $name = [IO.Path]::GetFileNameWithoutExtension($binary)
    $project = "p2boost-$name"
    $projectFile = Join-Path $projectRoot "$project.gpr"
    $args = @($projectRoot,$project)
    if ($Fresh -or -not (Test-Path -LiteralPath $projectFile)) { $args += @('-import',$binary) }
    else { $args += @('-process',[IO.Path]::GetFileName($binary),'-noanalysis') }
    $args += @('-scriptPath',$scriptRoot,'-postScript','ExportP2Analysis.java',$exportRoot,'-analysisTimeoutPerFile','900')
    & $headless @args
    if ($LASTEXITCODE -ne 0) { throw "Ghidra failed for $relative ($LASTEXITCODE)" }
}
