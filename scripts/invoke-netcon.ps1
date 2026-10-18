param(
	[Parameter(Mandatory)][string[]]$Command,
	[Parameter(Mandatory)][string]$Password,
	[int]$Port = 2121,
	[ValidateRange(1, 600)][int]$TimeoutSeconds = 30
)

# Sends console commands to a Portal 2 started with -netconport/-netconpassword
# (for example by scripts\bench.ps1 -KeepGameOpen) and prints the console
# output they produce. A trailing echo marks the end of that output.
$ErrorActionPreference = 'Stop'

$client = [Net.Sockets.TcpClient]::new('127.0.0.1', $Port)
try {
	$stream = $client.GetStream()
	$token  = 'p2netcon_mark_' + [Guid]::NewGuid().ToString('N')
	foreach($line in @("PASS $Password") + $Command + "echo $token") {
		$bytes = [Text.Encoding]::ASCII.GetBytes("$line`n")
		$stream.Write($bytes, 0, $bytes.Length)
	}

	$buffer   = New-Object byte[] 65536
	$text     = [Text.StringBuilder]::new()
	$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
	while((Get-Date) -lt $deadline) {
		while($stream.DataAvailable) {
			$count = $stream.Read($buffer, 0, $buffer.Length)
			[void]$text.Append([Text.Encoding]::ASCII.GetString($buffer, 0, $count))
		}
		$output = $text.ToString()
		$index  = $output.IndexOf($token)
		if($index -ge 0) {
			return $output.Substring(0, $index)
		}
		[Threading.Thread]::Sleep(20)
	}
	throw "Timed out waiting for: $($Command -join '; ')"
}
finally {
	$client.Dispose()
}
