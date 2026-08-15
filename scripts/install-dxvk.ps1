param(
    [string]$GameRoot = 'D:\SteamLibrary\steamapps\common\Portal 2',
    [string]$Version = '3.0.2',
    [string]$SourceDll = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $GameRoot 'bin'
$target = Join-Path $bin 'dxvk_d3d9.dll'
$backup = "$target.p2boost-original"
$temporary = Join-Path $env:TEMP "p2boost-dxvk-$Version"
$shippedHash = '7BF7C95D81FF33B951FF985037401FF5A792474F1E3B9C5D2F521AFFB92B9073'

$known = @{
    '3.0.2' = '44A2E749694128710CCE3A545C954BD9D986DD6A8EA61BB08D931C2EF0190488'
}
if (-not $known.ContainsKey($Version)) { throw "Version $Version is not approved. Known versions: $($known.Keys -join ', ')" }
$replacementHash = $known[$Version]
if (-not (Test-Path -LiteralPath $target)) { throw "game DXVK not found: $target" }
$current = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash

if ($current -eq $replacementHash) {
    Write-Host "bin\dxvk_d3d9.dll is already DXVK $Version (approved)."
    & (Join-Path $repo 'scripts\verify-build.ps1') -GameRoot $GameRoot
    exit 0
}
if ($current -ne $shippedHash) {
    throw "bin\dxvk_d3d9.dll is neither the shipped build nor an approved replacement; refusing to overwrite an unknown binary."
}

# keep one exact copy of the shipped binary so the game can always be restored.
if (-not (Test-Path -LiteralPath $backup)) {
    Copy-Item -LiteralPath $target -Destination $backup
    Write-Host "Backed up the shipped binary to $backup"
} elseif ((Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash -ne $shippedHash) {
    throw "backup $backup does not match the shipped build; remove it and re-run"
}

$source = $SourceDll
if (-not $source) {
    if (-not (Test-Path -LiteralPath $temporary)) { New-Item -ItemType Directory -Force $temporary | Out-Null }
    $archive = Join-Path $temporary "dxvk-$Version.tar.gz"
    $uri = "https://github.com/doitsujin/dxvk/releases/download/v$Version/dxvk-$Version.tar.gz"
    if (-not (Test-Path -LiteralPath $archive)) {
        Write-Host "Downloading DXVK $Version from GitHub..."
        Invoke-WebRequest -Uri $uri -OutFile $archive
    }
    & tar -xzf $archive -C $temporary
    $source = Join-Path $temporary "dxvk-$Version\x32\d3d9.dll"
    if (-not (Test-Path -LiteralPath $source)) { throw "32-bit D3D9 DLL not found in the release archive" }
}

$actual = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
if ($actual -ne $replacementHash) {
    throw "source DLL SHA-256 $actual does not match the approved DXVK $Version hash $replacementHash"
}

Copy-Item -LiteralPath $source -Destination $target -Force
$installed = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
if ($installed -ne $replacementHash) { throw 'installed DLL verification failed' }

Write-Host "Installed DXVK $Version (x86) as bin\dxvk_d3d9.dll"
Write-Host 'Restore the shipped binary any time with scripts\restore-dxvk.ps1'
Write-Host 'Note: some antivirus products flag DXVK binaries; the hash above matches the official release.'
& (Join-Path $repo 'scripts\verify-build.ps1') -GameRoot $GameRoot
