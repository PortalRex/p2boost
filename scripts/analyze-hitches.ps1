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
$captureEnd = [double]$frames[-1].CPUStartQPC +
    [double]$frames[-1].$frameTimeColumn * $frequency / 1000.0
$metrics = if ($MetricsCsv -and (Test-Path -LiteralPath $MetricsCsv)) {
    # The fixed metrics ring can contain a minute of high-FPS history and more
    # than two million CSV fields. Filter its chronological text rows by QPC
    # before constructing PowerShell objects so short captures analyze quickly.
    $metricHeader = Get-Content -LiteralPath $MetricsCsv -TotalCount 1
    $metricNames = $metricHeader.Split(',')
    $wantedNames = @(
        'qpc',
        'reacquire_ms',
        'portal_render_ms',
        'shadow_update_ms',
        'shadow_skipped_count',
        'render_worker_ms',
        'render_worker_end_qpc',
        'world_surface_ms',
        'world_surface_count',
        'world_surface_flags',
        'portal_movement_spatial_ms',
        'portal_movement_spatial_count',
        'animation_events_ms',
        'sound_lookup_ms',
        'read_ubit_var_ms',
        'focused'
    )
    $wantedColumns = [Collections.Generic.List[object]]::new()
    foreach ($name in $wantedNames) {
        $index = [Array]::IndexOf($metricNames, $name)
        if ($index -ge 0) {
            $wantedColumns.Add([pscustomobject]@{ Name = $name; Index = $index })
        }
    }
    $metricRows = [Collections.Generic.List[object]]::new()
    foreach ($line in [IO.File]::ReadLines([IO.Path]::GetFullPath($MetricsCsv))) {
        if ($line -eq $metricHeader) { continue }
        $separator = $line.IndexOf(',')
        if ($separator -le 0) { continue }
        $qpc = 0L
        if (-not [long]::TryParse($line.Substring(0, $separator), [ref]$qpc)) { continue }
        if ($qpc -ge $captureStart - $frequency -and $qpc -le $captureEnd + $frequency) {
            # Metrics contain only numeric and identifier fields. Avoid
            # ConvertFrom-Csv allocating every unused column in the wide trace.
            $values = $line.Split(',')
            $properties = [ordered]@{}
            foreach ($column in $wantedColumns) {
                if ($column.Index -lt $values.Count) {
                    $properties[$column.Name] = $values[$column.Index]
                }
            }
            $metricRows.Add([pscustomobject]$properties)
        }
    }
    @($metricRows)
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
    # p2boost commits pending work at CEngine::Frame completion, whereas
    # PresentMon timestamps the start of the same frame. Match against the
    # computed end QPC so short high-FPS frames cannot select the prior row.
    $frameEndQpc = $qpc + $frameMs * $frequency / 1000.0
    $cpuBusy = if ($frame.CPUBusy -ne 'NA') { [double]$frame.CPUBusy } else { 0.0 }
    $cpuWait = if ($frame.CPUWait -ne 'NA') { [double]$frame.CPUWait } else { 0.0 }
    $gpuBusy = if ($frame.GPUBusy -ne 'NA') { [double]$frame.GPUBusy } else { 0.0 }
    $metric = Find-NearestMetric $frameEndQpc
    $metricDistanceMs = if ($metric) {
        [Math]::Abs([double]$metric.qpc - $frameEndQpc) * 1000.0 / $frequency
    } else {
        [double]::NaN
    }
    $workerMs = if ($metric) { [double]$metric.render_worker_ms } else { 0.0 }
    $mainTailMs = if ($metric -and [long]$metric.render_worker_end_qpc -gt 0 -and
        [long]$metric.render_worker_end_qpc -le [long]$metric.qpc) {
        ([double]$metric.qpc - [double]$metric.render_worker_end_qpc) * 1000.0 / $frequency
    } else {
        0.0
    }

    # This label is deliberately coarse. ETW is still required to identify the
    # responsible function; the label prevents CPU work, waits, and GPU work
    # from being conflated before that trace is collected.
    $kind = if ($metric -and $metricDistanceMs -le 2.0 -and $workerMs -ge $frameMs * 0.7) {
        'render-worker'
    } elseif ($metric -and $metricDistanceMs -le 2.0 -and $mainTailMs -ge $frameMs * 0.5) {
        'main-tail'
    } elseif ($gpuBusy -ge $frameMs * 0.7) {
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
        WorkerMs = [Math]::Round($workerMs, 4)
        MainTailMs = [Math]::Round($mainTailMs, 4)
        WorldMs = if ($metric -and $metric.PSObject.Properties.Name -contains 'world_surface_ms') {
            [double]$metric.world_surface_ms
        } else { 0.0 }
        WorldCalls = if ($metric -and $metric.PSObject.Properties.Name -contains 'world_surface_count') {
            [int]$metric.world_surface_count
        } else { 0 }
        WorldFlags = if ($metric -and $metric.PSObject.Properties.Name -contains 'world_surface_flags') {
            $metric.world_surface_flags
        } else { '0x0' }
        SpatialMs = if ($metric -and $metric.PSObject.Properties.Name -contains 'portal_movement_spatial_ms') {
            [double]$metric.portal_movement_spatial_ms
        } else { 0.0 }
        SpatialCalls = if ($metric -and $metric.PSObject.Properties.Name -contains 'portal_movement_spatial_count') {
            [int]$metric.portal_movement_spatial_count
        } else { 0 }
        AnimationMs = if ($metric -and $metric.PSObject.Properties.Name -contains 'animation_events_ms') {
            [double]$metric.animation_events_ms
        } else { 0.0 }
        SoundMs = if ($metric -and $metric.PSObject.Properties.Name -contains 'sound_lookup_ms') {
            [double]$metric.sound_lookup_ms
        } else { 0.0 }
        BitReadMs = if ($metric -and $metric.PSObject.Properties.Name -contains 'read_ubit_var_ms') {
            [double]$metric.read_ubit_var_ms
        } else { 0.0 }
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

$largest |
    Select-Object RelativeMs, FrameMs, Kind, CPUBusyMs, CPUWaitMs, GPUBusyMs,
        WorkerMs, MainTailMs, PortalMs, ReacquireMs, MetricDistanceMs |
    Format-Table -AutoSize
