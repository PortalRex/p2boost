param([Parameter(Mandatory)][string]$InputCsv)
$ErrorActionPreference = 'Stop'
$rows = Import-Csv -LiteralPath $InputCsv
if ($rows.Count -lt 100) { throw "capture contains only $($rows.Count) frames" }
$column = if ($rows[0].PSObject.Properties.Name -contains 'CPUFrameTime') { 'CPUFrameTime' }
          elseif ($rows[0].PSObject.Properties.Name -contains 'FrameTime') { 'FrameTime' }
          elseif ($rows[0].PSObject.Properties.Name -contains 'MsBetweenPresents') { 'MsBetweenPresents' }
          else { throw 'No supported frame-time column found.' }
$values = @($rows | ForEach-Object { [double]($_.$column) } | Where-Object { $_ -gt 0 } | Sort-Object)
function Percentile([double[]]$Data, [double]$P) {
    $index = [Math]::Ceiling($P * $Data.Count) - 1
    return $Data[[Math]::Max(0, [Math]::Min($Data.Count - 1, $index))]
}
$median = Percentile $values 0.5
$p99 = Percentile $values 0.99
$p1Fps = 1000.0 / (Percentile $values 0.99)
$durationSeconds = ($values | Measure-Object -Sum).Sum / 1000.0
$cpuBusy = @($rows | ForEach-Object { [double]$_.CPUBusy } | Sort-Object)
$gpuBusy = @($rows | Where-Object { $_.GPUBusy -ne 'NA' } | ForEach-Object { [double]$_.GPUBusy } | Sort-Object)
$displayed = @($rows | Where-Object { $_.DisplayedTime -ne 'NA' } | ForEach-Object { [double]$_.DisplayedTime })
$repeatIntervals = @($displayed | Where-Object { $_ -gt 4.0 }).Count
[pscustomobject]@{
    File = (Resolve-Path $InputCsv).Path
    Frames = $values.Count
    AverageFps = [Math]::Round($values.Count / $durationSeconds, 2)
    MedianMs = [Math]::Round($median, 4)
    P95Ms = [Math]::Round((Percentile $values 0.95), 4)
    P99Ms = [Math]::Round($p99, 4)
    MaxMs = [Math]::Round($values[-1], 4)
    P1Fps = [Math]::Round($p1Fps, 2)
    P99ToMedian = [Math]::Round($p99 / $median, 4)
    CpuBusyMedianMs = [Math]::Round((Percentile $cpuBusy 0.5), 4)
    CpuBusyP99Ms = [Math]::Round((Percentile $cpuBusy 0.99), 4)
    GpuBusyMedianMs = if ($gpuBusy.Count) { [Math]::Round((Percentile $gpuBusy 0.5), 4) } else { 'NA' }
    GpuBusyP99Ms = if ($gpuBusy.Count) { [Math]::Round((Percentile $gpuBusy 0.99), 4) } else { 'NA' }
    DisplayRepeatPct = if ($displayed.Count) { [Math]::Round(100.0 * $repeatIntervals / $displayed.Count, 3) } else { 'NA' }
    Over8_3ms = @($values | Where-Object { $_ -gt 8.3 }).Count
    Over16_7ms = @($values | Where-Object { $_ -gt 16.7 }).Count
} | Format-List
