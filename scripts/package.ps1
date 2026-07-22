param([string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$destination = Join-Path $repo "out\p2boost-$Configuration"
if (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination -Recurse -Force }
New-Item -ItemType Directory -Force $destination | Out-Null
foreach ($name in '.clang-format','README.md','LICENSE','THIRD_PARTY_NOTICES.md','CMakeLists.txt','CMakePresets.json') {
    Copy-Item -LiteralPath (Join-Path $repo $name) -Destination $destination
}
foreach ($directory in 'src','tests','tools','scripts','cfg','.github') {
    Copy-Item -LiteralPath (Join-Path $repo $directory) -Destination $destination -Recurse
}
New-Item -ItemType Directory -Force (Join-Path $destination 'benchmarks') | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'benchmarks\report.md') -Destination (Join-Path $destination 'benchmarks')
New-Item -ItemType Directory -Force (Join-Path $destination 'research\ghidra') | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'research\report.md') -Destination (Join-Path $destination 'research')
Copy-Item -LiteralPath (Join-Path $repo 'research\ghidra\scripts') -Destination (Join-Path $destination 'research\ghidra') -Recurse
$binaryDirectory = Join-Path $repo "build\vs2022-x86\$Configuration"
Copy-Item -LiteralPath (Join-Path $binaryDirectory 'p2boost.dll') -Destination $destination
Copy-Item -LiteralPath (Join-Path $binaryDirectory 'p2boost.pdb') -Destination $destination
Copy-Item -LiteralPath (Join-Path $binaryDirectory 'p2profile.exe') -Destination $destination
$archive = "${destination}.zip"
# PowerShell's wildcard omits root dotfiles, so add the format policy
# explicitly while keeping the archive rooted at its contents.
Compress-Archive -Path "$destination\*", "$destination\.clang-format" -DestinationPath $archive -Force
Write-Host "Packaged $archive"
