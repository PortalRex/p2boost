param(
    [string]$GameRoot = 'D:\SteamLibrary\steamapps\common\Portal 2',
    [string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$source = Join-Path $repo "build\vs2022-x86\$Configuration\p2boost.dll"
$pdb = Join-Path $repo "build\vs2022-x86\$Configuration\p2boost.pdb"
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
$managedPattern = '(?ms)^// p2boost installer begin\r?\n.*?^// p2boost installer end'
if ($text -match $managedPattern) {
    $updated = [regex]::Replace($text, $managedPattern, $block)
    [IO.File]::WriteAllText($autoexec, $updated, [Text.UTF8Encoding]::new($false))
} else {
    $suffix = if ($text.EndsWith("`n")) { '' } else { "`r`n" }
    [IO.File]::WriteAllText($autoexec, $text + $suffix + $block + "`r`n", [Text.UTF8Encoding]::new($false))
}

Write-Host 'Installed the Vulkan-only p2boost DLL. Its first plugin load checks the NVIDIA DXVK profile.'
Write-Host "Launch Portal 2 with -vulkan."
