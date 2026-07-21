param(
    [Parameter(Mandatory)][string]$InputCsv,
    [int]$WindowMilliseconds = 100
)
$ErrorActionPreference = 'Stop'
$frames = Import-Csv -LiteralPath $InputCsv
$events = Import-Csv -LiteralPath ([IO.Path]::ChangeExtension($InputCsv, '.events.csv'))
$frequency = [Diagnostics.Stopwatch]::Frequency
$qpcColumn = if ($frames[0].PSObject.Properties.Name -contains 'CPUStartQPC') { 'CPUStartQPC' } else { 'CPUStartTime' }
$results = foreach ($event in $events) {
    $start = [double]$event.portal_qpc
    $end = $start + $WindowMilliseconds * $frequency / 1000.0
    $window = @($frames | Where-Object {
        $qpc = [double]($_.$qpcColumn)
        $qpc -ge $start -and $qpc -lt $end
    })
    $times = @($window | ForEach-Object { [double]$_.FrameTime })
    [pscustomobject]@{
        Cycle = [int]$event.cycle
        Frames = $times.Count
        FirstMs = if ($times.Count) { $times[0] } else { [double]::NaN }
        MaxMs = if ($times.Count) { ($times | Measure-Object -Maximum).Maximum } else { [double]::NaN }
    }
}
$valid = @($results | Where-Object { -not [double]::IsNaN($_.MaxMs) })
$sorted = @($valid.MaxMs | Sort-Object)
function Percentile([double[]]$values, [double]$p) {
    if (-not $values.Count) { return [double]::NaN }
    return $values[[Math]::Min($values.Count - 1, [Math]::Ceiling($p * $values.Count) - 1)]
}
[pscustomobject]@{
    Cycles = $valid.Count
    MedianPostPortalMaxMs = [Math]::Round((Percentile $sorted 0.5), 4)
    P95PostPortalMaxMs = [Math]::Round((Percentile $sorted 0.95), 4)
    WorstPostPortalMs = [Math]::Round((Percentile $sorted 1.0), 4)
    CyclesOver8_3ms = @($valid | Where-Object { $_.MaxMs -gt 8.3 }).Count
} | Format-List
$results | Format-Table -AutoSize
