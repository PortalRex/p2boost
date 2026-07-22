param(
    [string]$InputPath = 'D:\SteamLibrary\steamapps\common\Portal 2\tas\p2boost_sp_trust_raw.p2tas',
    [string]$OutputPath = 'D:\SteamLibrary\steamapps\common\Portal 2\tas\p2boost_sp_trust_now_raw.p2tas'
)

$ErrorActionPreference = 'Stop'

$source = Get-Content -LiteralPath $InputPath
if ($source.Count -lt 3 -or $source[0] -notmatch '^version \d+$' -or $source[1] -notmatch '^start map ') {
    throw 'The input is not a complete map-start P2TAS route.'
}

# Keep every raw controller tick intact. Only remove the map load so profiling
# can begin after `restart_level` has restored the same deterministic spawn.
$source[1] = 'start now'

# P2TAS is an ASCII text format. A temporary sibling keeps replacement atomic
# if the process is interrupted while the generated test route is being saved.
$temporary = "$OutputPath.tmp"
[IO.File]::WriteAllLines($temporary, $source, [Text.Encoding]::ASCII)
Move-Item -LiteralPath $temporary -Destination $OutputPath -Force

Write-Host "Prepared raw SP route: $OutputPath"
