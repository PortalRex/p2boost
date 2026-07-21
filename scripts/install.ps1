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
# Early development builds used the installation root. Remove only those
# p2boost-owned legacy paths so plugin_load cannot select a stale DLL.
foreach ($name in 'p2boost.dll','p2boost.pdb') {
    $legacy = Join-Path $GameRoot $name
    if (Test-Path -LiteralPath $legacy) { Remove-Item -LiteralPath $legacy -Force }
}
Get-ChildItem -LiteralPath (Join-Path $repo 'cfg') -Filter 'p2boost*.cfg' | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $GameRoot "portal2\cfg\$($_.Name)") -Force
}

$begin = '// p2boost installer begin'
$end = '// p2boost installer end'
$text = Get-Content -LiteralPath $autoexec -Raw
$block = "$begin`r`nplugin_load p2boost`r`n$end"
if ($text -notmatch [regex]::Escape($begin)) {
    $backup = "$autoexec.p2boost-backup"
    if (-not (Test-Path -LiteralPath $backup)) { Copy-Item -LiteralPath $autoexec -Destination $backup }
    $suffix = if ($text.EndsWith("`n")) { '' } else { "`r`n" }
    [IO.File]::WriteAllText($autoexec, $text + $suffix + $block + "`r`n", [Text.UTF8Encoding]::new($false))
}
Write-Host "Installed p2boost.dll and ensured 'plugin_load p2boost' is present."
