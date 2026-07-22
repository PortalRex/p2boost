param(
    [Parameter(Mandatory)][string]$Output,
    [ValidateRange(5,600)][int]$DurationSeconds = 10,
    [string]$ProcessName = 'portal2.exe'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$presentMon = Join-Path $repo '.tools\downloads\PresentMon.exe'
if (-not (Test-Path -LiteralPath $presentMon)) { throw 'Run scripts/bootstrap-tools.ps1 first.' }
$target = if ([IO.Path]::IsPathRooted($Output)) {
    [IO.Path]::GetFullPath($Output)
} else {
    [IO.Path]::GetFullPath((Join-Path $repo $Output))
}
$directory = Split-Path -Parent $target
New-Item -ItemType Directory -Force $directory | Out-Null
if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Force }
& $presentMon --process_name $ProcessName --output_file $target --v2_metrics --timed $DurationSeconds --terminate_after_timed --no_console_stats --set_circular_buffer_size 32768
if ($LASTEXITCODE -ne 0) { throw "PresentMon exited with $LASTEXITCODE" }
if (-not (Test-Path -LiteralPath $target) -or (Get-Item -LiteralPath $target).Length -eq 0) {
    throw "PresentMon produced no frames: $target"
}
