param([string]$GameRoot = 'D:\SteamLibrary\steamapps\common\Portal 2')
$expected = [ordered]@{
    'bin\engine.dll' = '5196EACE78D5F2F47F8CE39A49B0EFD15374A2B996B272642F0493ACE42C24DC'
    'bin\shaderapidx9.dll' = '0879D15B4F9CF9F55BB86D874DC88B6EDF4D8DFC825E77D3BD1619A2DF250439'
    'bin\materialsystem.dll' = '8B8BBDB77E72A23BEF8E67F0B9800327338E125BA0FBFAE68FEB2DC864AF4FBF'
    'portal2\bin\client.dll' = '2FF27C2878C258B2C16A0982E30B0D2F1BAD9D213E42CC778DC2A2925C625137'
    'portal2\bin\server.dll' = '4C3ECC83BBEA0864660A49322D1D5188CDC00FE43A7EA56F1CA8A0C94356BB4C'
}
$failed = $false
foreach ($entry in $expected.GetEnumerator()) {
    $path = Join-Path $GameRoot $entry.Key
    $actual = if (Test-Path -LiteralPath $path) { (Get-FileHash $path -Algorithm SHA256).Hash } else { 'MISSING' }
    $ok = $actual -eq $entry.Value
    if (-not $ok) { $failed = $true }
    [pscustomobject]@{ Module=$entry.Key; Supported=$ok; Actual=$actual }
}
if ($failed) { exit 1 }

