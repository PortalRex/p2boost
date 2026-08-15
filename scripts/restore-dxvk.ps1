param([string]$GameRoot = 'D:\SteamLibrary\steamapps\common\Portal 2')
$ErrorActionPreference = 'Stop'
$target = Join-Path $GameRoot 'bin\dxvk_d3d9.dll'
$backup = "$target.p2boost-original"
$shippedHash = '7BF7C95D81FF33B951FF985037401FF5A792474F1E3B9C5D2F521AFFB92B9073'

if (-not (Test-Path -LiteralPath $backup)) {
    throw "no backup exists: $backup"
}
if (-not (Test-Path -LiteralPath $target)) {
    throw "game DXVK not found: $target"
}
$backupHash = (Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash
if ($backupHash -ne $shippedHash) {
    throw "backup $backup does not match the shipped build; refusing to restore an unknown binary"
}
$current = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
if ($current -eq $shippedHash) {
    Write-Host "bin\dxvk_d3d9.dll is already the shipped build."
    Remove-Item -LiteralPath $backup -Force
    Write-Host "Removed backup $backup"
    exit 0
}

Copy-Item -LiteralPath $backup -Destination $target -Force
$restored = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
if ($restored -ne $shippedHash) { throw 'restored DLL verification failed' }
Remove-Item -LiteralPath $backup -Force
Write-Host 'Restored the shipped bin\dxvk_d3d9.dll and removed the backup.'
