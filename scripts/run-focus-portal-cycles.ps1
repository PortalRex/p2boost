param(
    [string]$GameRoot = 'D:\SteamLibrary\steamapps\common\Portal 2',
    [string]$Output = 'artifacts\benchmarks\focus-portal.csv',
    [int]$Cycles = 30
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$game = Join-Path $GameRoot 'portal2.exe'
$presentMon = Join-Path $repo '.tools\downloads\PresentMon.exe'
$outputPath = [IO.Path]::GetFullPath((Join-Path $repo $Output))
New-Item -ItemType Directory -Force (Split-Path $outputPath) | Out-Null
if (-not ('P2BoostFocusCycle' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class P2BoostFocusCycle {
    [DllImport("user32.dll")] public static extern bool ShowWindowAsync(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
}
'@
}
function Get-GameWindow {
    $process = Get-Process portal2 -ErrorAction Stop |
        Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } |
        Select-Object -First 1
    if (-not $process) { throw 'Portal 2 has no top-level window.' }
    return $process.MainWindowHandle
}
function Send-GameCommand([string]$command) {
    $process = Start-Process -FilePath $game -ArgumentList "-hijack $command" `
        -WorkingDirectory $GameRoot -WindowStyle Hidden -PassThru
    $process.WaitForExit(5000) | Out-Null
}

$duration = [Math]::Max(20, [int]($Cycles * 0.9 + 8))
$arguments = @(
    '--process_name', 'portal2.exe', '--output_file', $outputPath,
    '--v2_metrics', '--timed', $duration, '--terminate_after_timed',
    '--no_console_stats', '--set_circular_buffer_size', '32768'
)
$capture = Start-Process -FilePath $presentMon -ArgumentList $arguments `
    -WorkingDirectory $repo -WindowStyle Hidden -PassThru
Start-Sleep -Seconds 2
for ($index = 0; $index -lt $Cycles; ++$index) {
    $window = Get-GameWindow
    [P2BoostFocusCycle]::ShowWindowAsync($window, 6) | Out-Null
    Start-Sleep -Milliseconds 250
    [P2BoostFocusCycle]::ShowWindowAsync($window, 9) | Out-Null
    [P2BoostFocusCycle]::SetForegroundWindow($window) | Out-Null
    Start-Sleep -Milliseconds 150
    Send-GameCommand '+exec p2boost_portal_cycle'
    Start-Sleep -Milliseconds 250
}
$capture.WaitForExit()
if ($capture.ExitCode -ne 0) { throw "PresentMon exited with $($capture.ExitCode)" }
Write-Host "Completed $Cycles focus/portal-shot cycles: $outputPath"
