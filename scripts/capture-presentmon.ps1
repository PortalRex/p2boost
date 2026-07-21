param(
    [Parameter(Mandatory)][string]$Output,
    [int]$DurationSeconds = 60,
    [string]$ProcessName = 'portal2.exe'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$presentMon = Join-Path $repo '.tools\downloads\PresentMon.exe'
if (-not (Test-Path -LiteralPath $presentMon)) { throw 'Run scripts/bootstrap-tools.ps1 first.' }
$directory = Split-Path -Parent ([IO.Path]::GetFullPath($Output))
New-Item -ItemType Directory -Force $directory | Out-Null
& $presentMon --process_name $ProcessName --output_file $Output --v2_metrics --timed $DurationSeconds --terminate_after_timed --no_console_stats --set_circular_buffer_size 32768
if ($LASTEXITCODE -ne 0) { throw "PresentMon exited with $LASTEXITCODE" }
