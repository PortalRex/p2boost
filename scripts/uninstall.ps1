param([string]$GameRoot = 'D:\SteamLibrary\steamapps\common\Portal 2')
$ErrorActionPreference = 'Stop'
$autoexec = Join-Path $GameRoot 'portal2\cfg\autoexec.cfg'
$pluginDirectory = Join-Path $GameRoot 'portal2'
$driver = Join-Path $pluginDirectory 'p2boost_driver.exe'
$driverBackup = Join-Path $pluginDirectory 'p2boost_driver_profile.state'

# Restore only values captured by the installer. If restoration fails, keep
# the helper and backup in place so the original profile is still recoverable.
if ((Test-Path -LiteralPath $driver) -and (Test-Path -LiteralPath $driverBackup)) {
    $driverProcess = Start-Process -FilePath $driver -Verb RunAs -ArgumentList 'restore' -Wait -PassThru
    if ($driverProcess.ExitCode -ne 0) {
        throw "NVIDIA profile restoration failed with exit code $($driverProcess.ExitCode)"
    }
}

if (Test-Path -LiteralPath $autoexec) {
    $text = Get-Content -LiteralPath $autoexec -Raw
    $pattern = '(?ms)^// p2boost installer begin\r?\nplugin_load p2boost\r?\n// p2boost installer end\r?\n?'
    $updated = [regex]::Replace($text, $pattern, '')
    if ($updated -ne $text) { [IO.File]::WriteAllText($autoexec, $updated, [Text.UTF8Encoding]::new($false)) }
}
foreach ($directory in $GameRoot,(Join-Path $GameRoot 'portal2')) {
    foreach ($name in 'p2boost.dll','p2boost.pdb','p2boost_driver.exe','p2boost_driver_profile.state') {
        $path = Join-Path $directory $name
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    }
}
Write-Host 'Restored the NVIDIA profile and removed the managed autoexec block and p2boost binaries.'
