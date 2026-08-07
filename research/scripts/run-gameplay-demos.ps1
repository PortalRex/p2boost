param(
    [ValidateSet('All','TrustFling','CoopPaint','CoopTractorBeam')]
    [string]$Scenario = 'All',
    [ValidateRange(5,60)][int]$DurationSeconds = 5,
    [ValidateRange(0,15)][int]$WarmupSeconds = 2,
    [int]$FpsMax = 999,
    [int]$ProcessId = 0,
    [string]$RconPassword = 'p2boostlab',
    [string]$OutputDirectory = 'artifacts\benchmarks\gameplay',
    [switch]$PatchedOnly,
    [switch]$KeepGameOpen
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$rcon = Join-Path $repo 'scripts\invoke-rcon.ps1'
$capture = Join-Path $PSScriptRoot 'capture-presentmon.ps1'
$analyze = Join-Path $repo 'scripts\analyze-presentmon.ps1'
$stopGame = Join-Path $repo 'scripts\stop-portal2.ps1'
$process = if ($ProcessId) { Get-Process -Id $ProcessId -ErrorAction Stop } else {
    Get-Process portal2 -ErrorAction Stop | Select-Object -First 1
}
$output = [IO.Path]::GetFullPath((Join-Path $repo $OutputDirectory))
New-Item -ItemType Directory -Force $output | Out-Null
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class P2BoostGameplayForeground {
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
}
'@

$scenarios = @(
    [pscustomobject]@{ Name='TrustFling'; Slug='trust-fling'; Demo='chamber_40-91'; Map='sp_a2_trust_fling' },
    [pscustomobject]@{ Name='CoopPaint'; Slug='coop-paint'; Demo='wall repulsion'; Map='mp_coop_paint_walljumps' },
    [pscustomobject]@{ Name='CoopTractorBeam'; Slug='coop-tractor-beam'; Demo='chamber_35-40'; Map='mp_coop_tbeam_drill' }
)
if ($Scenario -ne 'All') { $scenarios = @($scenarios | Where-Object Name -eq $Scenario) }

function Invoke-Game([string[]]$Commands, [int]$Timeout = 5) {
    & $rcon -ProcessId $process.Id -Password $RconPassword `
        -ResponseTimeoutSeconds $Timeout -Command $Commands
}
function Read-Cvar([string]$Name) {
    $text = (Invoke-Game @($Name) | Out-String)
    $match = [regex]::Match($text, '=\s*"([^"]+)"')
    if (-not $match.Success) { throw "Could not read $Name from: $text" }
    $match.Groups[1].Value
}
function Start-VerifiedDemo($Case) {
    $escapedDemo = $Case.Demo.Replace('"', '')
    $reply = Invoke-Game @("playdemo `"$escapedDemo`"") 10 | Out-String
    if ($reply -notmatch ('Map:\s+' + [regex]::Escape($Case.Map))) {
        throw "Demo header did not report expected map $($Case.Map): $reply"
    }
}

$runtime = Invoke-Game @('p2boost_status') | Out-String
if ($runtime -notmatch 'supported=1' -or $runtime -notmatch 'dxvk_vulkan=1') {
    throw "p2boost runtime verification failed: $runtime"
}
$originalFpsMax = Read-Cvar 'fps_max'
$variants = if ($PatchedOnly) { @([pscustomobject]@{ Name='patched'; Enabled=1 }) } else {
    @([pscustomobject]@{ Name='unpatched'; Enabled=0 },
      [pscustomobject]@{ Name='patched'; Enabled=1 })
}
try {
    Invoke-Game @("fps_max $FpsMax", 'p2boost_trace 0') | Out-Null
    foreach ($case in $scenarios) {
        foreach ($variant in $variants) {
            Invoke-Game @("p2boost_enable $($variant.Enabled)") | Out-Null
            Start-VerifiedDemo $case
            $process.Refresh()
            [P2BoostGameplayForeground]::ShowWindow($process.MainWindowHandle, 9) | Out-Null
            [P2BoostGameplayForeground]::SetForegroundWindow($process.MainWindowHandle) | Out-Null
            if ($WarmupSeconds) { Start-Sleep -Seconds $WarmupSeconds }
            $target = Join-Path $output "$($case.Slug)-$($variant.Name).csv"
            & $capture -Output $target -DurationSeconds $DurationSeconds
            & $analyze -Input $target
            try { Invoke-Game @('stopdemo') 2 | Out-Null } catch {}
            Start-Sleep -Milliseconds 500
        }
    }
} finally {
    try {
        Invoke-Game @('stopdemo', 'p2boost_trace 0', 'p2boost_enable 1',
                      "fps_max $originalFpsMax") 2 | Out-Null
    } catch {}
    if (-not $KeepGameOpen) { & $stopGame -ProcessId $process.Id }
}
Write-Host "Gameplay demo matrix complete: $output"
