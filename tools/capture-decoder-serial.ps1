param(
  [Parameter(Mandatory = $true)]
  [string]$Port,

  [int]$Baud = 115200,

  [string]$OutFile = "",

  [int]$DurationSeconds = 0
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($OutFile)) {
  $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
  $logDir = Join-Path (Split-Path -Parent $PSScriptRoot) "logs"
  $OutFile = Join-Path $logDir "decoder-$stamp.log"
}

$resolvedOut = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutFile)
$resolvedDir = Split-Path -Parent $resolvedOut
New-Item -ItemType Directory -Path $resolvedDir -Force | Out-Null

$serial = [System.IO.Ports.SerialPort]::new(
  $Port,
  $Baud,
  [System.IO.Ports.Parity]::None,
  8,
  [System.IO.Ports.StopBits]::One
)
$serial.ReadTimeout = 500
$serial.NewLine = "`n"

$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$writer = [System.IO.StreamWriter]::new($resolvedOut, $true, $utf8NoBom)
$start = Get-Date

Write-Host "Logging $Port at $Baud baud to $resolvedOut"
if ($DurationSeconds -gt 0) {
  Write-Host "Stopping after $DurationSeconds seconds."
} else {
  Write-Host "Press Ctrl+C to stop."
}

try {
  $serial.Open()

  while ($true) {
    if ($DurationSeconds -gt 0 -and ((Get-Date) - $start).TotalSeconds -ge $DurationSeconds) {
      break
    }

    try {
      $line = $serial.ReadLine().TrimEnd("`r", "`n")
      $entry = "{0:o} {1}" -f (Get-Date), $line
      Write-Host $entry
      $writer.WriteLine($entry)
      $writer.Flush()
    } catch [System.TimeoutException] {
    }
  }
} finally {
  if ($serial.IsOpen) {
    $serial.Close()
  }
  $writer.Dispose()
}
