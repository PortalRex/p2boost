param(
    [string]$Output = 'artifacts\etw\portal2-cpu.etl',
    [ValidateRange(5,600)][int]$DurationSeconds = 30,
    [string]$ProcessName = 'portal2',
    [switch]$SkipPresentMon,
    [ValidateSet('CPU','GeneralProfile','GPU')][string]$Profile = 'CPU'
)
$ErrorActionPreference = 'Stop'
$principal = [Security.Principal.WindowsPrincipal]::new(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'ETW kernel profiling requires an elevated PowerShell session.'
}
$repo = Split-Path -Parent $PSScriptRoot
$target = if ([IO.Path]::IsPathRooted($Output)) {
    [IO.Path]::GetFullPath($Output)
} else {
    [IO.Path]::GetFullPath((Join-Path $repo $Output))
}
New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
$game = Get-Process $ProcessName -ErrorAction SilentlyContinue |
    Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } | Select-Object -First 1
if (-not $game) {
    throw "Start Portal 2 and wait for its window before running this capture (process '$ProcessName' not found)."
}
function Invoke-Wpr([string]$Arguments) {
    $wpr = Join-Path $env:SystemRoot 'System32\wpr.exe'
    $process = Start-Process -FilePath $wpr -ArgumentList $Arguments `
        -Wait -PassThru -NoNewWindow
    return $process.ExitCode
}
$started = $false
$presentMonProcess = $null
try {
    # WPR can leave a recorder session registered after an interrupted merge.
    # Starting over that stale session may return success yet omit CPU samples.
    # Cancel is idempotent for our private capture process; a nonzero result only
    # means there was no usable session to cancel, so the fresh start remains the
    # authoritative operation below.
    Invoke-Wpr '-cancel' | Out-Null

    Write-Host "Starting WPR $Profile capture for $ProcessName (pid=$($game.Id))..."
    $startExit = Invoke-Wpr "-start $Profile -filemode"
    if ($startExit -ne 0) { throw "WPR start failed with $startExit" }
    $started = $true
    if (-not $SkipPresentMon) {
        $presentMon = Join-Path $repo '.tools\downloads\PresentMon.exe'
        if (-not (Test-Path -LiteralPath $presentMon)) {
            throw 'PresentMon is missing; run scripts\bootstrap-tools.ps1 or pass -SkipPresentMon.'
        }
        $presentMonTarget = [IO.Path]::ChangeExtension($target, '.presentmon.csv')
        if (Test-Path -LiteralPath $presentMonTarget) {
            Remove-Item -LiteralPath $presentMonTarget -Force
        }
        # An interrupted benchmark can leave PresentMon's global trace session
        # alive even though its frontend process is gone. Reclaim that session
        # explicitly so an elevated ETW run cannot silently produce no CSV.
        $presentMonArguments = "--process_name $ProcessName.exe --output_file `"$presentMonTarget`" --v2_metrics --qpc_time --stop_existing_session --timed $DurationSeconds --terminate_after_timed --no_console_stats --set_circular_buffer_size 32768"
        $presentMonProcess = Start-Process -FilePath $presentMon -ArgumentList $presentMonArguments `
            -WindowStyle Hidden -PassThru
        Write-Host "PresentMon frame capture: $presentMonTarget"
    }
    Write-Host "Recording for $DurationSeconds seconds. Switch to Portal 2 and reproduce the hitch now."
    for ($remaining = $DurationSeconds; $remaining -gt 0; --$remaining) {
        if ($remaining -eq $DurationSeconds -or $remaining -eq 1 -or $remaining % 5 -eq 0) {
            Write-Host "  $remaining seconds remaining..."
        }
        if (-not (Get-Process -Id $game.Id -ErrorAction SilentlyContinue)) {
            throw 'Portal 2 exited during the ETW capture.'
        }
        Start-Sleep -Seconds 1
    }
    if ($presentMonProcess) {
        $presentMonProcess.WaitForExit()
        $presentMonProcess.Refresh()
        if ($presentMonProcess.ExitCode -ne 0) {
            throw "PresentMon failed with exit code $($presentMonProcess.ExitCode)."
        }
    }
    Write-Host 'Finalizing ETL; the file can remain at zero bytes while WPR merges the trace...'
    $stopExit = Invoke-Wpr "-stop `"$target`""
    if ($stopExit -ne 0) { throw "WPR stop failed with $stopExit" }
    $started = $false
    $etl = Get-Item -LiteralPath $target -ErrorAction Stop
    if ($etl.Length -eq 0) { throw "WPR produced an empty ETL: $target" }
    Write-Host "ETW capture written: $target"
    Write-Host ("  ETL size: {0:N1} MiB" -f ($etl.Length / 1MB))
    if (-not $SkipPresentMon) {
        $presentCsv = Get-Item -LiteralPath $presentMonTarget -ErrorAction Stop
        if ($presentCsv.Length -eq 0) { throw "PresentMon produced an empty CSV: $presentMonTarget" }
        Write-Host "  PresentMon: $($presentCsv.FullName)"
    }
} catch {
    if ($presentMonProcess -and -not $presentMonProcess.HasExited) {
        Stop-Process -Id $presentMonProcess.Id -Force -ErrorAction SilentlyContinue
    }
    if ($started) { Invoke-Wpr '-cancel' | Out-Null }
    throw
}
