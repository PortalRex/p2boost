param(
    [string]$Configuration = 'Release',
    [string]$BuildDirectory = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$destination = Join-Path $repo "out\p2boost-$Configuration"
if (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination -Recurse -Force }
New-Item -ItemType Directory -Force $destination | Out-Null
foreach ($name in '.clang-format','README.md','LICENSE','THIRD_PARTY_NOTICES.md','CMakeLists.txt','CMakePresets.json') {
    Copy-Item -LiteralPath (Join-Path $repo $name) -Destination $destination
}
foreach ($directory in 'src','tests','scripts','third_party','.github') {
    Copy-Item -LiteralPath (Join-Path $repo $directory) -Destination $destination -Recurse
}
New-Item -ItemType Directory -Force (Join-Path $destination 'benchmarks') | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'benchmarks\report.md') -Destination (Join-Path $destination 'benchmarks')

# Package reproducible research source, never local Ghidra databases or derived
# analysis artifacts from ignored directories.
$researchDestination = Join-Path $destination 'research'
New-Item -ItemType Directory -Force $researchDestination | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'research\README.md') -Destination $researchDestination
Copy-Item -LiteralPath (Join-Path $repo 'research\report.md') -Destination $researchDestination
foreach ($directory in 'benchmarks','cfg','runtime','scripts','tools') {
    Copy-Item -LiteralPath (Join-Path $repo "research\$directory") -Destination $researchDestination -Recurse
}
New-Item -ItemType Directory -Force (Join-Path $researchDestination 'ghidra') | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'research\ghidra\scripts') `
    -Destination (Join-Path $researchDestination 'ghidra') -Recurse

if (-not $BuildDirectory) {
    $candidate = Get-ChildItem (Join-Path $repo 'build') -Directory -ErrorAction SilentlyContinue |
        ForEach-Object { Get-Item (Join-Path $_.FullName "$Configuration\p2boost.dll") -ErrorAction SilentlyContinue } |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $candidate) { throw "No $Configuration p2boost build was found." }
    $BuildDirectory = $candidate.DirectoryName
}
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'p2boost.dll') -Destination $destination
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'p2boost.pdb') -Destination $destination
$archive = "${destination}.zip"
# enumerate hidden and ordinary root entries once so PowerShell versions that
# include dotfiles in wildcards cannot duplicate the format policy.
$archiveItems = Get-ChildItem -LiteralPath $destination -Force | Select-Object -ExpandProperty FullName
Compress-Archive -LiteralPath $archiveItems -DestinationPath $archive -Force
Write-Host "Packaged $archive"
