param([string]$GameRoot = 'D:\SteamLibrary\steamapps\common\Portal 2')
$expected = [ordered]@{
    'bin\engine.dll' = '5196EACE78D5F2F47F8CE39A49B0EFD15374A2B996B272642F0493ACE42C24DC'
    'portal2\bin\client.dll' = '2FF27C2878C258B2C16A0982E30B0D2F1BAD9D213E42CC778DC2A2925C625137'
    'bin\shaderapivk.dll' = 'AAEFA05D9C02049A0CCA67A9181F71F5A7710FCC21474BD55C0E1DEABBA76E84'
    'bin\dxvk_d3d9.dll' = '7BF7C95D81FF33B951FF985037401FF5A792474F1E3B9C5D2F521AFFB92B9073'
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

# Do not inherit an unrelated native tool's stale $LASTEXITCODE when this
# verifier is called from a longer benchmark or installation session.
exit 0
