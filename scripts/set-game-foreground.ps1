param(
    [Parameter(Mandatory)][int]$ProcessId,
    [ValidateRange(1, 10)][int]$TimeoutSeconds = 5
)

$ErrorActionPreference = 'Stop'

if (-not ('P2BoostForegroundWindow' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class P2BoostForegroundWindow {
    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);

    [DllImport("kernel32.dll")]
    public static extern uint GetCurrentThreadId();

    [DllImport("user32.dll")]
    public static extern bool AttachThreadInput(uint first, uint second, bool attach);

    [DllImport("user32.dll")]
    public static extern bool ShowWindowAsync(IntPtr window, int command);

    [DllImport("user32.dll")]
    public static extern bool BringWindowToTop(IntPtr window);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr window);
}
'@
}

$game = Get-Process -Id $ProcessId -ErrorAction Stop
if ($game.MainWindowHandle -eq [IntPtr]::Zero) {
    throw "Portal 2 process $ProcessId has no main window."
}

$shell = New-Object -ComObject WScript.Shell
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
do {
    # AppActivate follows Windows' foreground-activation policy and restores a
    # minimized window without injecting input or changing game configuration.
    [void]$shell.AppActivate($ProcessId)

    # Windows may reject activation when another process currently owns the
    # input queue. Temporarily attach this script's thread to that queue, make
    # the game visible, and request the foreground handoff through user32.
    $foreground = [P2BoostForegroundWindow]::GetForegroundWindow()
    [uint32]$ignoredProcessId = 0
    $foregroundThread = [P2BoostForegroundWindow]::GetWindowThreadProcessId(
        $foreground,
        [ref]$ignoredProcessId)
    $currentThread = [P2BoostForegroundWindow]::GetCurrentThreadId()
    $attached = $foregroundThread -ne 0 -and $foregroundThread -ne $currentThread -and
        [P2BoostForegroundWindow]::AttachThreadInput($currentThread, $foregroundThread, $true)
    try {
        [void][P2BoostForegroundWindow]::ShowWindowAsync($game.MainWindowHandle, 9)
        [void][P2BoostForegroundWindow]::BringWindowToTop($game.MainWindowHandle)
        [void][P2BoostForegroundWindow]::SetForegroundWindow($game.MainWindowHandle)
    } finally {
        if ($attached) {
            [void][P2BoostForegroundWindow]::AttachThreadInput($currentThread, $foregroundThread, $false)
        }
    }
    Start-Sleep -Milliseconds 100

    $foreground = [P2BoostForegroundWindow]::GetForegroundWindow()
    [uint32]$foregroundProcessId = 0
    [void][P2BoostForegroundWindow]::GetWindowThreadProcessId(
        $foreground,
        [ref]$foregroundProcessId)

    if ($foregroundProcessId -eq $ProcessId) {
        return
    }
} until ((Get-Date) -ge $deadline)

throw "Portal 2 did not become the foreground window (expected pid $ProcessId, got $foregroundProcessId)."
