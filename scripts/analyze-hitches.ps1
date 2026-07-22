param(
    [Parameter(Mandatory)][string]$InputCsv,
    [string]$MetricsCsv = '',
    [double]$ThresholdMilliseconds = 8.3,
    [ValidateRange(1, 100)][int]$Top = 25
)

$ErrorActionPreference = 'Stop'

$frames = @(Import-Csv -LiteralPath $InputCsv)
if ($frames.Count -lt 100) {
    throw "Capture contains only $($frames.Count) frames."
}

# PresentMon's first swap-chain interval starts before observation and is not a
# measured gameplay frame. Remove only that undefined boundary row.
$frames = @($frames |
    Group-Object ProcessID, SwapChainAddress |
    ForEach-Object {
        @($_.Group | Sort-Object { [double]$_.CPUStartQPC }) | Select-Object -Skip 1
    } |
    Sort-Object { [double]$_.CPUStartQPC })

if (-not ($frames[0].PSObject.Properties.Name -contains 'CPUStartQPC')) {
    throw 'Capture must include PresentMon --qpc_time data.'
}

$frameTimeColumn = if ($frames[0].PSObject.Properties.Name -contains 'CPUFrameTime') {
    'CPUFrameTime'
} elseif ($frames[0].PSObject.Properties.Name -contains 'FrameTime') {
    'FrameTime'
} else {
    throw 'No supported frame-time column was found.'
}

$frequency = [Diagnostics.Stopwatch]::Frequency
$captureStart = [double]$frames[0].CPUStartQPC
$metrics = if ($MetricsCsv -and (Test-Path -LiteralPath $MetricsCsv)) {
    @(Import-Csv -LiteralPath $MetricsCsv | Sort-Object { [double]$_.qpc })
} else {
    @()
}

function Find-NearestMetric([double]$Qpc) {
    if (-not $metrics.Count) {
        return $null
    }

    # Binary search keeps a 65,536-frame metrics ring cheap to correlate with
    # every outlier while retaining the closest chronological engine sample.
    $low = 0
    $high = $metrics.Count - 1
    while ($low -le $high) {
        $middle = [int](($low + $high) / 2)
        $value = [double]$metrics[$middle].qpc
        if ($value -lt $Qpc) {
            $low = $middle + 1
        } elseif ($value -gt $Qpc) {
            $high = $middle - 1
        } else {
            return $metrics[$middle]
        }
    }

    $candidates = @()
    if ($low -lt $metrics.Count) { $candidates += $metrics[$low] }
    if ($high -ge 0) { $candidates += $metrics[$high] }
    return $candidates | Sort-Object { [Math]::Abs([double]$_.qpc - $Qpc) } | Select-Object -First 1
}

$rows = foreach ($frame in $frames) {
    $frameMs = [double]$frame.$frameTimeColumn
    if ($frameMs -le 0) { continue }

    $qpc = [double]$frame.CPUStartQPC
    $cpuBusy = if ($frame.CPUBusy -ne 'NA') { [double]$frame.CPUBusy } else { 0.0 }
    $cpuWait = if ($frame.CPUWait -ne 'NA') { [double]$frame.CPUWait } else { 0.0 }
    $gpuBusy = if ($frame.GPUBusy -ne 'NA') { [double]$frame.GPUBusy } else { 0.0 }
    $metric = Find-NearestMetric $qpc
    $metricDistanceMs = if ($metric) {
        [Math]::Abs([double]$metric.qpc - $qpc) * 1000.0 / $frequency
    } else {
        [double]::NaN
    }

    # This label is deliberately coarse. ETW is still required to identify the
    # responsible function; the label prevents CPU work, waits, and GPU work
    # from being conflated before that trace is collected.
    $kind = if ($gpuBusy -ge $frameMs * 0.7) {
        'gpu-busy'
    } elseif ($cpuBusy -ge $frameMs * 0.7) {
        'cpu-busy'
    } elseif ($cpuWait -ge $frameMs * 0.5) {
        'cpu-wait'
    } else {
        'mixed'
    }

    [pscustomobject]@{
        RelativeMs = [Math]::Round(($qpc - $captureStart) * 1000.0 / $frequency, 3)
        FrameMs = [Math]::Round($frameMs, 4)
        CPUBusyMs = [Math]::Round($cpuBusy, 4)
        CPUWaitMs = [Math]::Round($cpuWait, 4)
        GPUBusyMs = [Math]::Round($gpuBusy, 4)
        Kind = $kind
        MetricDistanceMs = if ([double]::IsNaN($metricDistanceMs)) { 'NA' } else { [Math]::Round($metricDistanceMs, 4) }
        PortalMs = if ($metric) { [double]$metric.portal_render_ms } else { 0.0 }
        ShadowMs = if ($metric) { [double]$metric.shadow_update_ms } else { 0.0 }
        ShadowSkips = if ($metric) { [int]$metric.shadow_skipped_count } else { 0 }
        WorkerMs = if ($metric) { [double]$metric.render_worker_ms } else { 0.0 }
        ReacquireMs = if ($metric) { [double]$metric.reacquire_ms } else { 0.0 }
        Focused = if ($metric) { [int]$metric.focused } else { -1 }
        PresentMode = $frame.PresentMode
        Qpc = [long]$qpc
    }
}

$outliers = @($rows | Where-Object { $_.FrameMs -gt $ThresholdMilliseconds })
$largest = @($rows | Sort-Object FrameMs -Descending | Select-Object -First $Top)
$output = [IO.Path]::ChangeExtension([IO.Path]::GetFullPath($InputCsv), '.hitches.csv')
$largest | Export-Csv -LiteralPath $output -NoTypeInformation

$values = @($rows.FrameMs | Sort-Object)
$median = $values[[int](($values.Count - 1) * 0.5)]
$largestFrame = $values[-1]
[pscustomobject]@{
    Frames = $rows.Count
    MedianMs = [Math]::Round($median, 4)
    MaximumMs = [Math]::Round($largestFrame, 4)
    OverThreshold = $outliers.Count
    ThresholdMs = $ThresholdMilliseconds
    Report = $output
} | Format-List

$largest | Format-Table -AutoSize
