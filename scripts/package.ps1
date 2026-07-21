param([string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$destination = Join-Path $repo "out\p2boost-$Configuration"
if (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination -Recurse -Force }
New-Item -ItemType Directory -Force $destination | Out-Null
foreach ($name in 'README.md','LICENSE','THIRD_PARTY_NOTICES.md','CMakeLists.txt','CMakePresets.json') {
    Copy-Item -LiteralPath (Join-Path $repo $name) -Destination $destination
}
foreach ($directory in 'src','tests','scripts','cfg','benchmarks') {
    Copy-Item -LiteralPath (Join-Path $repo $directory) -Destination $destination -Recurse
}
New-Item -ItemType Directory -Force (Join-Path $destination 'research\ghidra') | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'research\report.md') -Destination (Join-Path $destination 'research')
Copy-Item -LiteralPath (Join-Path $repo 'research\ghidra\scripts') -Destination (Join-Path $destination 'research\ghidra') -Recurse
$binaryDirectory = Join-Path $repo "build\vs2022-x86\$Configuration"
Copy-Item -LiteralPath (Join-Path $binaryDirectory 'p2boost.dll') -Destination $destination
Copy-Item -LiteralPath (Join-Path $binaryDirectory 'p2boost.pdb') -Destination $destination
$archive = "${destination}.zip"
Compress-Archive -Path "$destination\*" -DestinationPath $archive -Force
Write-Host "Packaged $archive"
