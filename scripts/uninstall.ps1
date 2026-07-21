param([string]$GameRoot = 'D:\SteamLibrary\steamapps\common\Portal 2')
$ErrorActionPreference = 'Stop'
$autoexec = Join-Path $GameRoot 'portal2\cfg\autoexec.cfg'
if (Test-Path -LiteralPath $autoexec) {
    $text = Get-Content -LiteralPath $autoexec -Raw
    $pattern = '(?ms)^// p2boost installer begin\r?\nplugin_load p2boost\r?\n// p2boost installer end\r?\n?'
    $updated = [regex]::Replace($text, $pattern, '')
    if ($updated -ne $text) { [IO.File]::WriteAllText($autoexec, $updated, [Text.UTF8Encoding]::new($false)) }
}
foreach ($directory in $GameRoot,(Join-Path $GameRoot 'portal2')) {
    foreach ($name in 'p2boost.dll','p2boost.pdb') {
        $path = Join-Path $directory $name
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    }
}
Get-ChildItem -LiteralPath (Join-Path $GameRoot 'portal2\cfg') -Filter 'p2boost*.cfg' -ErrorAction SilentlyContinue |
    Remove-Item -Force
Write-Host 'Removed the managed autoexec block and p2boost binaries.'
