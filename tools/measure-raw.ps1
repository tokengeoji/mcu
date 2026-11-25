param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^COM[1-9][0-9]*$')]
    [string]$Port,

    [Parameter(Mandatory = $true)]
    [ValidateSet('Empty', 'S1', 'S2', 'S3', 'S4', 'S5')]
    [string]$Scene,

    [ValidateRange(1, 2147483647)]
    [int]$ReferenceMm,

    [ValidateRange(1, 2147483647)]
    [int]$Samples = 10,

    [ValidateNotNullOrEmpty()]
    [string]$OutFile
)

$ErrorActionPreference = 'Stop'
$Port = $Port.ToUpperInvariant()
$Scene = if ($Scene -ieq 'Empty') { 'Empty' } else { $Scene.ToUpperInvariant() }
$reference = if ($PSBoundParameters.ContainsKey('ReferenceMm')) { [string]$ReferenceMm } else { '' }
if (!$OutFile) {
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
    $id = [Guid]::NewGuid().ToString('N').Substring(0, 8)
    $OutFile = "raw-$Scene-$stamp-$id.csv"
}
$path = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutFile)
if ([IO.File]::Exists($path) -or [IO.Directory]::Exists($path)) {
    throw "Output already exists: $path"
}

function Read-RawReply($Serial) {
    $line = [Text.StringBuilder]::new()
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.ElapsedMilliseconds -lt 2500) {
        if ($Serial.BytesToRead -eq 0) {
            Start-Sleep -Milliseconds 10
            continue
        }
        $byte = $Serial.ReadByte()
        if ($byte -eq 10 -or $byte -eq 13) {
            $reply = $line.ToString()
            $null = $line.Clear()
            if ($reply -eq '' -or $reply -ceq 'ERR DISPLAY') { continue }
            if ($reply -cnotmatch '^R(?:,(?:-1|[0-9]{1,10})){5}$') {
                throw "Unexpected RAW response: $reply"
            }
            $distances = $reply.Substring(2)
            foreach ($field in $distances.Split(',')) {
                $value = 0
                if (![int]::TryParse($field, [ref]$value)) { throw "Invalid distance: $field" }
            }
            return $distances
        }
        if ($byte -lt 32 -or $byte -gt 126 -or $line.Length -ge 64) {
            throw 'Invalid or overlong serial line.'
        }
        $null = $line.Append([char]$byte)
    }
    throw 'No complete RAW response within 2.5 seconds.'
}

$serial = [IO.Ports.SerialPort]::new($Port, 9600, [IO.Ports.Parity]::None, 8, [IO.Ports.StopBits]::One)
$serial.Handshake = [IO.Ports.Handshake]::None
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$serial.ReadTimeout = 100
$serial.WriteTimeout = 1000
$writer = $null
$file = $null
$saved = 0
try {
    $serial.Open()
    for ($sample = 0; $sample -lt $Samples; $sample++) {
        if ($sample -gt 0) { Start-Sleep -Milliseconds 500 }
        $serial.DiscardInBuffer()
        $serial.Write("RAW`r`n")
        $distances = Read-RawReply $serial
        $received = [DateTime]::UtcNow.ToString('o')
        if (!$writer) {
            $file = [IO.File]::Open($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
            $writer = [IO.StreamWriter]::new($file, [Text.UTF8Encoding]::new($false))
            $writer.WriteLine('received_utc,scene,reference_mm,S1_mm,S2_mm,S3_mm,S4_mm,S5_mm')
        }
        $writer.WriteLine("$received,$Scene,$reference,$distances")
        $writer.Flush()
        $saved++
    }
} catch {
    if ($saved -gt 0) { Write-Warning "Kept $saved completed sample(s) in $path" }
    throw
} finally {
    try {
        if ($writer) { $writer.Dispose() } elseif ($file) { $file.Dispose() }
    } finally {
        $serial.Dispose()
    }
}
Write-Output "Saved $saved sample(s) to $path"
