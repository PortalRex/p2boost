param(
    [string]$GameRoot = 'D:\SteamLibrary\steamapps\common\Portal 2',
    [string]$Configuration = 'Release',
    [string]$BuildDirectory = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $BuildDirectory) {
    $candidate = Get-ChildItem (Join-Path $repo 'build') -Directory -ErrorAction SilentlyContinue |
        ForEach-Object { Get-Item (Join-Path $_.FullName "$Configuration\p2boost.dll") -ErrorAction SilentlyContinue } |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $candidate) { throw "No $Configuration p2boost build was found." }
    $BuildDirectory = $candidate.DirectoryName
}
$source = Join-Path $BuildDirectory 'p2boost.dll'
$pdb = Join-Path $BuildDirectory 'p2boost.pdb'
$autoexec = Join-Path $GameRoot 'portal2\cfg\autoexec.cfg'
$pluginDirectory = Join-Path $GameRoot 'portal2'
if (-not (Test-Path -LiteralPath $source)) { throw "build not found: $source" }
if (-not (Test-Path -LiteralPath $autoexec)) { throw "autoexec not found: $autoexec" }

Copy-Item -LiteralPath $source -Destination (Join-Path $pluginDirectory 'p2boost.dll') -Force
if (Test-Path -LiteralPath $pdb) { Copy-Item -LiteralPath $pdb -Destination (Join-Path $pluginDirectory 'p2boost.pdb') -Force }
# Remove helper artifacts left by versions that configured NVAPI externally.
foreach ($name in 'p2boost_driver.exe','p2boost_driver_profile.state') {
    $legacy = Join-Path $pluginDirectory $name
    if (Test-Path -LiteralPath $legacy) { Remove-Item -LiteralPath $legacy -Force }
}
# Early development builds used the installation root. Remove only those
# p2boost-owned legacy paths so plugin_load cannot select a stale DLL.
foreach ($name in 'p2boost.dll','p2boost.pdb') {
    $legacy = Join-Path $GameRoot $name
    if (Test-Path -LiteralPath $legacy) { Remove-Item -LiteralPath $legacy -Force }
}
$begin = '// p2boost installer begin'
$end = '// p2boost installer end'
$text = Get-Content -LiteralPath $autoexec -Raw
$block = "$begin`r`nplugin_load p2boost`r`n$end"
$backup = "$autoexec.p2boost-backup"
if (-not (Test-Path -LiteralPath $backup)) {
    Copy-Item -LiteralPath $autoexec -Destination $backup
}
$managedPattern = '(?ms)^// p2boost installer begin\r?\n.*?^// p2boost installer end\r?\n?'
$legacyLoadPattern = '(?im)^\s*plugin_load\s+"?(?:portal2[\\/])?p2boost(?:\.dll)?"?\s*$\r?\n?'
$updated = [regex]::Replace($text, $managedPattern, '')
$updated = [regex]::Replace($updated, $legacyLoadPattern, '')
$suffix = if ($updated.Length -eq 0 -or $updated.EndsWith("`n")) { '' } else { "`r`n" }
$updated = $updated + $suffix + $block + "`r`n"
if ($updated -ne $text) {
    [IO.File]::WriteAllText($autoexec, $updated, [Text.UTF8Encoding]::new($false))
}

Write-Host 'Installed one managed Vulkan-only p2boost load entry. NVIDIA profile setup runs only when Portal 2 uses an NVIDIA adapter.'
Write-Host "Launch Portal 2 with -vulkan."
