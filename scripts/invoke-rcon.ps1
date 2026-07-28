param(
    [Parameter(Mandatory)][string[]]$Command,
    [string]$Password = 'p2boostlab',
    [int]$ProcessId = 0,
    # Wide research CSV dumps can legitimately take longer than ordinary
    # console commands; keep the default short while permitting that caller.
    [ValidateRange(1,60)][int]$ResponseTimeoutSeconds = 5
)
$ErrorActionPreference = 'Stop'
$process = if ($ProcessId) { Get-Process -Id $ProcessId -ErrorAction Stop } else {
    Get-Process portal2 -ErrorAction Stop | Select-Object -First 1
}
$listener = Get-NetTCPConnection -OwningProcess $process.Id -State Listen -ErrorAction SilentlyContinue |
    Where-Object LocalPort -eq 27015 | Select-Object -First 1
if (-not $listener) { throw 'Portal 2 RCON listener not found; launch with -usercon.' }
$client = [Net.Sockets.TcpClient]::new($listener.LocalAddress, 27015)
$stream = $client.GetStream()
$stream.ReadTimeout = $ResponseTimeoutSeconds * 1000
$stream.WriteTimeout = $ResponseTimeoutSeconds * 1000
function Send-Packet([int]$id, [int]$type, [string]$payload) {
    $text = [Text.Encoding]::ASCII.GetBytes($payload)
    $writer = [IO.BinaryWriter]::new($stream, [Text.Encoding]::ASCII, $true)
    $writer.Write([int](10 + $text.Length)); $writer.Write($id); $writer.Write($type)
    $writer.Write($text); $writer.Write([byte]0); $writer.Write([byte]0); $writer.Flush()
}
function Read-Packet {
    $sizeBytes = New-Object byte[] 4
    if ($stream.Read($sizeBytes, 0, 4) -ne 4) { throw 'RCON connection closed.' }
    $size = [BitConverter]::ToInt32($sizeBytes, 0)
    $data = New-Object byte[] $size
    $offset = 0
    while ($offset -lt $size) {
        $read = $stream.Read($data, $offset, $size - $offset)
        if ($read -le 0) { throw 'RCON connection closed mid-packet.' }
        $offset += $read
    }
    [pscustomobject]@{
        Id = [BitConverter]::ToInt32($data, 0)
        Type = [BitConverter]::ToInt32($data, 4)
        Text = if ($size -gt 10) { [Text.Encoding]::ASCII.GetString($data, 8, $size - 10) } else { '' }
    }
}
try {
    Send-Packet 1 3 $Password
    $first = Read-Packet; $auth = Read-Packet
    if ($first.Id -eq -1 -or $auth.Id -eq -1) { throw 'RCON authentication failed.' }
    $id = 10
    foreach ($item in $Command) {
        Send-Packet $id 2 $item
        $text = [Text.StringBuilder]::new()
        $received = $false
        $quietSince = $null
        $deadline = [Diagnostics.Stopwatch]::GetTimestamp() +
            [Diagnostics.Stopwatch]::Frequency * $ResponseTimeoutSeconds
        while ([Diagnostics.Stopwatch]::GetTimestamp() -lt $deadline) {
            if ($stream.DataAvailable) {
                $reply = Read-Packet
                if ($reply.Text) { [void]$text.Append($reply.Text) }
                if ($reply.Id -eq $id) {
                    $received = $true
                }
                $quietSince = $null
                continue
            }
            if ($received) {
                if ($null -eq $quietSince) { $quietSince = [Diagnostics.Stopwatch]::GetTimestamp() }
                if (([Diagnostics.Stopwatch]::GetTimestamp() - $quietSince) * 1000 / [Diagnostics.Stopwatch]::Frequency -ge 75) { break }
            }
            Start-Sleep -Milliseconds 5
        }
        if (-not $received) { throw "No RCON response for: $item" }
        if ($text.Length) { $text.ToString() }
        ++$id
    }
} finally {
    $client.Dispose()
}
