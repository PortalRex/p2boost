param(
    [string]$Output = 'artifacts\etw\portal2-cpu.etl',
    [int]$DurationSeconds = 60,
    [ValidateSet('CPU','GeneralProfile','GPU')][string]$Profile = 'CPU'
)
$ErrorActionPreference = 'Stop'
$principal = [Security.Principal.WindowsPrincipal]::new(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'ETW kernel profiling requires an elevated Codex worker process.'
}
$repo = Split-Path -Parent $PSScriptRoot
$target = [IO.Path]::GetFullPath((Join-Path $repo $Output))
New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
wpr.exe -cancel 2>$null | Out-Null
try {
    & wpr.exe -start $Profile -filemode
    if ($LASTEXITCODE -ne 0) { throw "WPR start failed with $LASTEXITCODE" }
    $deadline = (Get-Date).AddSeconds($DurationSeconds)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds ([Math]::Min(10, [Math]::Ceiling(($deadline - (Get-Date)).TotalSeconds)))
    }
    & wpr.exe -stop $target
    if ($LASTEXITCODE -ne 0) { throw "WPR stop failed with $LASTEXITCODE" }
    Write-Host "ETW capture written: $target"
} catch {
    & wpr.exe -cancel 2>$null | Out-Null
    throw
}
