param(
    [Parameter(Mandatory)][string]$InputEtl,
    [string]$PresentMonCsv = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$etl = (Resolve-Path -LiteralPath $InputEtl).Path
if (-not $PresentMonCsv) {
    $PresentMonCsv = [IO.Path]::ChangeExtension($etl, '.presentmon.csv')
}
$xperf = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Windows Performance Toolkit\xperf.exe'
if (-not (Test-Path -LiteralPath $xperf)) {
    throw 'xperf.exe is missing; install the Windows Performance Toolkit.'
}
$stem = Join-Path (Split-Path $etl) ([IO.Path]::GetFileNameWithoutExtension($etl))
$images = "$stem-images.csv"
$modules = "$stem-modules.tsv"
$disk = "$stem-disk-summary.txt"
$profile = "$stem-profile.txt"
$report = "$stem-summary.txt"

function Invoke-Xperf([string[]]$Arguments) {
    & $xperf @Arguments
    if ($LASTEXITCODE -ne 0) { throw "xperf failed with exit code $LASTEXITCODE" }
}

# Kernel image-load provider. The rundown records the authoritative ASLR base
# for every module, so sampled addresses must never be mapped from guesses.
Invoke-Xperf @('-i', $etl, '-tle', '-o', $images, '-a', 'dumper', '-provider',
    '{2cb15d1d-5fc1-11d2-abe1-00a0c911f518}')
Invoke-Xperf @('-i', $etl, '-tle', '-o', $disk, '-a', 'diskio', '-summary')
Invoke-Xperf @('-i', $etl, '-tle', '-o', $profile, '-a', 'stack', '-butterfly', '5',
    '-process', 'portal2.exe', '-event', 'Profile')

$moduleRows = foreach ($line in Get-Content -LiteralPath $images) {
    if ($line -notmatch 'I-DCEnd,.*portal2\.exe.*?0x([0-9a-fA-F]+),\s*0x([0-9a-fA-F]+).*?"([^"]+\.dll)"') { continue }
    [pscustomobject]@{
        Module = [IO.Path]::GetFileName($Matches[3])
        Base = '0x' + $Matches[1].ToUpperInvariant()
        End = '0x' + $Matches[2].ToUpperInvariant()
        Path = $Matches[3]
    }
}
$moduleRows | Sort-Object Module | Export-Csv -Delimiter "`t" -NoTypeInformation -LiteralPath $modules

$text = [Text.StringBuilder]::new()
[void]$text.AppendLine("ETL: $etl")
[void]$text.AppendLine("Modules: $modules")
[void]$text.AppendLine("CPU profile: $profile")
[void]$text.AppendLine("Disk summary: $disk")
if (Test-Path -LiteralPath $PresentMonCsv) {
    [void]$text.AppendLine()
    [void]$text.AppendLine('PresentMon:')
    $frameSummary = & (Join-Path $PSScriptRoot 'analyze-presentmon.ps1') -InputCsv $PresentMonCsv | Out-String
    [void]$text.AppendLine($frameSummary.TrimEnd())
}
[void]$text.AppendLine()
[void]$text.AppendLine('Portal 2 disk files:')
$portalDisk = Select-String -Path $disk -Pattern 'Portal 2' | ForEach-Object { $_.Line.Trim() }
if ($portalDisk) { foreach ($line in $portalDisk) { [void]$text.AppendLine($line) } }
else { [void]$text.AppendLine('(no physical Portal 2 file I/O recorded)') }
[IO.File]::WriteAllText($report, $text.ToString())
Write-Host $text.ToString()
Write-Host "Analysis written: $report"
