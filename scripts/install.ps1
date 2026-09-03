param(
    [string]$GameRoot = 'D:\SteamLibrary\steamapps\common\Portal 2',
    [string]$Configuration = 'Release',
    [string]$BuildDirectory = '',
    [switch]$SkipDxvk
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

# every official DXVK x86 build we redistribute must match an approved hash.
$knownDxvk = @{
    '3.0.2' = '44A2E749694128710CCE3A545C954BD9D986DD6A8EA61BB08D931C2EF0190488'
}
$dxvkVersion = '3.0.2'

if (-not $BuildDirectory) {
    $candidate = Get-ChildItem (Join-Path $repo 'build') -Directory -ErrorAction SilentlyContinue |
        ForEach-Object { Get-Item (Join-Path $_.FullName "$Configuration\p2boost.dll") -ErrorAction SilentlyContinue } |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $candidate) { throw "No $Configuration p2boost build was found." }
    $BuildDirectory = $candidate.DirectoryName
}
$source = Join-Path $BuildDirectory 'p2boost.dll'
$pdb = Join-Path $BuildDirectory 'p2boost.pdb'
$pluginDirectory = $GameRoot
if (-not (Test-Path -LiteralPath $source)) { throw "build not found: $source" }

# The base game executes portal2\cfg\autoexec.cfg; mods with their own client
# (e.g. Portal Stories Mel) execute the mod directory's cfg\autoexec.cfg.
$autoexec = $null
foreach ($candidate in @((Join-Path $GameRoot 'portal2\cfg\autoexec.cfg'), (Join-Path $GameRoot 'portal_stories\cfg\autoexec.cfg'))) {
    if (Test-Path -LiteralPath $candidate) { $autoexec = $candidate; break }
}
if (-not $autoexec) { throw "no autoexec.cfg was found under $GameRoot" }

# Plugin DLLs go in the game root beside portal2.exe and sar.dll; Source finds
# them there before any game-directory copy.
Copy-Item -LiteralPath $source -Destination (Join-Path $pluginDirectory 'p2boost.dll') -Force
if (Test-Path -LiteralPath $pdb) { Copy-Item -LiteralPath $pdb -Destination (Join-Path $pluginDirectory 'p2boost.pdb') -Force }
# Remove helper artifacts left by versions that configured NVAPI externally.
foreach ($name in 'p2boost_driver.exe','p2boost_driver_profile.state') {
    $legacy = Join-Path $pluginDirectory $name
    if (Test-Path -LiteralPath $legacy) { Remove-Item -LiteralPath $legacy -Force }
}
# Older releases installed into the portal2 game directory. Remove only those
# p2boost-owned legacy paths so plugin_load cannot select a stale DLL.
foreach ($name in 'p2boost.dll','p2boost.pdb') {
    $legacy = Join-Path $GameRoot ("portal2\$name")
    if (Test-Path -LiteralPath $legacy) { Remove-Item -LiteralPath $legacy -Force }
}

# Install the approved DXVK 3.0.2 x86 build beside the shipped backend. The
# plugin accepts it as a verified replacement for performance measurements.
if (-not $SkipDxvk) {
    $dxvkHash = $knownDxvk[$dxvkVersion]
    $target = Join-Path $GameRoot "bin\dxvk_d3d9.dll"
    $existing = if (Test-Path -LiteralPath $target) { (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash } else { '' }
    if ($existing -ne $dxvkHash) {
        $temporary = Join-Path $repo '.tools\dxvk-package'
        New-Item -ItemType Directory -Force $temporary | Out-Null
        $archive = Join-Path $temporary "dxvk-$dxvkVersion.tar.gz"
        if (-not (Test-Path -LiteralPath $archive)) {
            Write-Host "Downloading DXVK $dxvkVersion from GitHub..."
            Invoke-WebRequest -Uri "https://github.com/doitsujin/dxvk/releases/download/v$dxvkVersion/dxvk-$dxvkVersion.tar.gz" -OutFile $archive
        }
        & tar -xzf $archive -C $temporary
        $source = Join-Path $temporary "dxvk-$dxvkVersion\x32\d3d9.dll"
        if (-not (Test-Path -LiteralPath $source)) { throw "32-bit D3D9 DLL not found in the DXVK release archive" }
        $actual = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
        if ($actual -ne $dxvkHash) {
            throw "DXVK $dxvkVersion SHA-256 $actual does not match the approved hash $dxvkHash"
        }
        Copy-Item -LiteralPath $source -Destination $target -Force
        $license = Join-Path $GameRoot 'LICENSE.dxvk.txt'
        if (-not (Test-Path -LiteralPath $license)) {
            Invoke-WebRequest -Uri "https://raw.githubusercontent.com/doitsujin/dxvk/v$dxvkVersion/LICENSE" -OutFile $license
        }
        Write-Host "Installed approved DXVK $dxvkVersion as bin\dxvk_d3d9.dll."
    } else {
        Write-Host "DXVK $dxvkVersion already installed as bin\dxvk_d3d9.dll."
    }
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

Write-Host 'Installed one managed Vulkan-only p2boost load entry. NVIDIA profile setup runs only when the game uses an NVIDIA adapter.'
Write-Host 'Launch the game with -vulkan.'
