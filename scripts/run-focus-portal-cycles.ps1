param(
    [string]$GameRoot = 'D:\SteamLibrary\steamapps\common\Portal 2',
    [string]$Output = 'artifacts\benchmarks\focus-portal.csv',
    [string]$RconPassword = 'p2boostlab',
    [ValidateRange(1,1000)][int]$Cycles = 5,
    [ValidateSet('AltTab','Minimize')][string]$FocusMode = 'AltTab',
    [switch]$TraceP2Boost,
    [switch]$KeepGameOpen
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$presentMon = Join-Path $repo '.tools\downloads\PresentMon.exe'
$rcon = Join-Path $PSScriptRoot 'invoke-rcon.ps1'
$stopGame = Join-Path $PSScriptRoot 'stop-portal2.ps1'
$outputPath = [IO.Path]::GetFullPath((Join-Path $repo $Output))
$eventPath = [IO.Path]::ChangeExtension($outputPath, '.events.csv')
New-Item -ItemType Directory -Force (Split-Path $outputPath) | Out-Null
foreach ($oldOutput in @($outputPath, $eventPath)) {
    if (Test-Path -LiteralPath $oldOutput) { Remove-Item -LiteralPath $oldOutput -Force }
}
function Get-GameProcess {
    $process = Get-Process portal2 -ErrorAction Stop |
        Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } | Select-Object -First 1
    if (-not $process) { throw 'Portal 2 has no top-level window.' }
    return $process
}
$gameProcess = Get-GameProcess
try {
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class P2BoostFocusCaptureWindow {
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern IntPtr MonitorFromWindow(IntPtr window, uint flags);
    [DllImport("user32.dll", CharSet = CharSet.Auto)]
    public static extern bool GetMonitorInfo(IntPtr monitor, ref MonitorInfo info);

    public struct Rect { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Auto)]
    public struct MonitorInfo {
        public int Size;
        public Rect Monitor;
        public Rect Work;
        public uint Flags;
    }
}
'@
[P2BoostFocusCaptureWindow]::ShowWindow($gameProcess.MainWindowHandle, 9) | Out-Null
[P2BoostFocusCaptureWindow]::SetForegroundWindow($gameProcess.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 500

# The focus regression is specific to fullscreen-sized device restoration. A
# windowed run is a different presentation path and must not silently pass.
$windowRect = [P2BoostFocusCaptureWindow+Rect]::new()
[P2BoostFocusCaptureWindow]::GetWindowRect($gameProcess.MainWindowHandle, [ref]$windowRect) | Out-Null
$monitorInfo = [P2BoostFocusCaptureWindow+MonitorInfo]::new()
$monitorInfo.Size = [Runtime.InteropServices.Marshal]::SizeOf($monitorInfo)
$monitor = [P2BoostFocusCaptureWindow]::MonitorFromWindow($gameProcess.MainWindowHandle, 2)
[P2BoostFocusCaptureWindow]::GetMonitorInfo($monitor, [ref]$monitorInfo) | Out-Null
$coversMonitor = $windowRect.Left -eq $monitorInfo.Monitor.Left -and `
    $windowRect.Top -eq $monitorInfo.Monitor.Top -and `
    $windowRect.Right -eq $monitorInfo.Monitor.Right -and `
    $windowRect.Bottom -eq $monitorInfo.Monitor.Bottom
if (-not $coversMonitor) {
    throw 'Portal 2 must cover the selected monitor for the fullscreen focus test.'
}
$listener = Get-NetTCPConnection -OwningProcess $gameProcess.Id -State Listen -ErrorAction Stop |
    Where-Object { $_.LocalPort -eq 27015 } | Select-Object -First 1
if (-not $listener) { throw 'Launch Portal 2 with -usercon and a non-empty rcon_password.' }
# Each cycle includes one RCON setup round trip in addition to 2.45 seconds of
# focus and delayed input. Budget its observed worst case so a 30-cycle release
# run cannot outlive PresentMon on a busy machine.
$duration = [Math]::Max(10, [int]([Math]::Ceiling($Cycles * 3.25 + 8)))
$arguments = @(
    '--process_id', $gameProcess.Id, '--output_file', $outputPath, '--v2_metrics',
    '--qpc_time', '--timed', $duration, '--terminate_after_timed',
    '--no_console_stats', '--set_circular_buffer_size', '32768'
)
$actionScript = Join-Path $PSScriptRoot 'invoke-focus-portal-actions.ps1'
$actionArgs = "-NoProfile -ExecutionPolicy Bypass -File `"$actionScript`" -ProcessId $($gameProcess.Id) -Cycles $Cycles -EventPath `"$eventPath`" -FocusMode $FocusMode -RconPassword `"$RconPassword`""
if ($TraceP2Boost) { $actionArgs += ' -TraceP2Boost' }
$actions = Start-Process powershell.exe -ArgumentList $actionArgs -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput "$eventPath.out.txt" -RedirectStandardError "$eventPath.err.txt"
& $presentMon @arguments
$presentExit = $LASTEXITCODE
$actions.WaitForExit()
$actions.Refresh()
$actionExit = $actions.ExitCode
$errorText = Get-Content "$eventPath.err.txt" -Raw -ErrorAction SilentlyContinue
if ($null -ne $actionExit -and $actionExit -ne 0) {
    throw "focus action helper exited with ${actionExit}: $errorText"
}
if ($null -eq $actionExit -and $errorText) {
    throw "focus action helper failed without an exit code: $errorText"
}
if ($presentExit -ne 0) { throw "PresentMon exited with $presentExit" }
$events = @(Import-Csv -LiteralPath $eventPath)
$frames = @(Import-Csv -LiteralPath $outputPath)
if ($events.Count -ne $Cycles) {
    throw "Focus helper recorded $($events.Count) of $Cycles requested cycles."
}
if (-not $frames.Count -or -not ($frames[0].PSObject.Properties.Name -contains 'CPUStartQPC')) {
    throw 'PresentMon produced no usable QPC-timestamped frames.'
}
$firstFrameQpc = [double]$frames[0].CPUStartQPC
$lastFrameQpc = [double]$frames[-1].CPUStartQPC
$firstPortalQpc = [double]$events[0].portal_qpc
$lastPortalEndQpc = [double]$events[-1].portal_qpc + 0.1 * [Diagnostics.Stopwatch]::Frequency
if ($firstFrameQpc -gt $firstPortalQpc -or $lastFrameQpc -lt $lastPortalEndQpc) {
    throw "PresentMon did not overlap the complete action window (frames $firstFrameQpc..$lastFrameQpc; events $firstPortalQpc..$lastPortalEndQpc)."
}
Write-Host "Completed $Cycles timestamped focus/portal cycles: $outputPath"
} finally {
    if (-not $KeepGameOpen) { & $stopGame -ProcessId $gameProcess.Id }
}
