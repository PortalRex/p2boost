param(
    [string]$Configuration = 'Release',
    [string]$BuildDirectory = '',
    [string]$DxvkVersion = '3.0.2',
    [string]$Output = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

# every official DXVK x86 build we redistribute must match an approved hash.
$known = @{
    '3.0.2' = '44A2E749694128710CCE3A545C954BD9D986DD6A8EA61BB08D931C2EF0190488'
}
if (-not $known.ContainsKey($DxvkVersion)) {
    throw "DXVK $DxvkVersion is not approved. Known versions: $($known.Keys -join ', ')"
}
$dxvkHash = $known[$DxvkVersion]

if (-not $BuildDirectory) {
    $candidate = Get-ChildItem (Join-Path $repo 'build') -Directory -ErrorAction SilentlyContinue |
        ForEach-Object { Get-Item (Join-Path $_.FullName "$Configuration\p2boost.dll") -ErrorAction SilentlyContinue } |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $candidate) { throw "No $Configuration p2boost build was found." }
    $BuildDirectory = $candidate.DirectoryName
}
if (-not $Output) { $Output = Join-Path $repo "out\p2boost-dropin-win32.zip" }

$temporary = Join-Path $repo '.tools\dxvk-package'
New-Item -ItemType Directory -Force $temporary | Out-Null
New-Item -ItemType Directory -Force (Split-Path $Output) | Out-Null

# stage the exact layout of a Portal 2 installation so the zip can be extracted
# straight into the game root.
$stage = Join-Path $temporary 'stage'
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Force (Join-Path $stage 'bin') | Out-Null
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'p2boost.dll') -Destination $stage -Force
Copy-Item -LiteralPath (Join-Path $repo 'README.md') -Destination $stage -Force
Copy-Item -LiteralPath (Join-Path $repo 'THIRD_PARTY_NOTICES.md') -Destination $stage -Force
Copy-Item -LiteralPath (Join-Path $repo 'LICENSE') -Destination (Join-Path $stage 'LICENSE.txt') -Force

$archive = Join-Path $temporary "dxvk-$DxvkVersion.tar.gz"
$uri = "https://github.com/doitsujin/dxvk/releases/download/v$DxvkVersion/dxvk-$DxvkVersion.tar.gz"
if (-not (Test-Path -LiteralPath $archive)) {
    Write-Host "Downloading DXVK $DxvkVersion from GitHub..."
    Invoke-WebRequest -Uri $uri -OutFile $archive
}
& tar -xzf $archive -C $temporary
$source = Join-Path $temporary "dxvk-$DxvkVersion\x32\d3d9.dll"
if (-not (Test-Path -LiteralPath $source)) { throw "32-bit D3D9 DLL not found in the release archive" }
$actual = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
if ($actual -ne $dxvkHash) {
    throw "DXVK $DxvkVersion SHA-256 $actual does not match the approved hash $dxvkHash"
}
Copy-Item -LiteralPath $source -Destination (Join-Path $stage 'bin\dxvk_d3d9.dll') -Force

# The build archive contains no license text; fetch the exact zlib license from
# the matching source tag so the redistributed binary keeps its notice.
$license = Join-Path $temporary "dxvk-$DxvkVersion-LICENSE"
if (-not (Test-Path -LiteralPath $license)) {
    $licenseUri = "https://raw.githubusercontent.com/doitsujin/dxvk/v$DxvkVersion/LICENSE"
    Invoke-WebRequest -Uri $licenseUri -OutFile $license
}
if (-not (Test-Path -LiteralPath $license) -or (Get-Item -LiteralPath $license).Length -eq 0) {
    throw "DXVK license could not be fetched from the v$DxvkVersion source tag"
}
Copy-Item -LiteralPath $license -Destination (Join-Path $stage 'LICENSE.dxvk.txt') -Force

# Pass the top-level entries once: file entries are stored by their names while
# directory entries keep their structure, which is exactly the game layout.
$stageItems = Get-ChildItem -LiteralPath $stage -Force | Select-Object -ExpandProperty FullName
Remove-Item -LiteralPath $Output -Force -ErrorAction SilentlyContinue
Compress-Archive -LiteralPath $stageItems -DestinationPath $Output -Force
$hash = (Get-FileHash -LiteralPath $Output -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $(Split-Path -Leaf $Output)" | Set-Content -LiteralPath "$Output.sha256" -Encoding ascii
Write-Host "Packaged $Output"
Write-Host "Layout: p2boost.dll, bin\dxvk_d3d9.dll, README.md, LICENSE.txt, THIRD_PARTY_NOTICES.md, LICENSE.dxvk.txt"
