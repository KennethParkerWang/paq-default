$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$benchmarkRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Resolve-Path (Join-Path $benchmarkRoot '..\..\..')
$originalCodecPath = 'F:\paq8px\PaqBenchStudio\staging-v1.1.0\paq8px.exe'
$derivedCodecPath = Join-Path $projectRoot 'verification\build\paq8pxsd.exe'
$fixtureDirectory = Join-Path $benchmarkRoot 'inputs'
$archiveDirectory = Join-Path $benchmarkRoot 'archives'
$restoredDirectory = Join-Path $benchmarkRoot 'restored'
$logDirectory = Join-Path $benchmarkRoot 'logs'
$resultPath = Join-Path $benchmarkRoot 'results.csv'

$expectedOriginalHash = 'F79343702F596A4FA6C7CC3E25F2FA9C05199EAF11F06655B065F687E5F42533'
$expectedDerivedHash = '4BAE2C77D1C9BA8EB4B6F2E435DE140E3BA9BE26C8D9CE4FD0662ED09BF5C8D5'
$fileNames = @(
  'dickens', 'mozilla', 'mr', 'nci', 'ooffice', 'osdb',
  'reymont', 'samba', 'sao', 'webster', 'x-ray', 'xml'
)

function Get-BlockTypes([string[]] $lines) {
  $types = @()
  foreach ($line in $lines) {
    if ($line -match '^\s*\d+\s+\|\s*([^|]+?)\s*\|') {
      $types += $Matches[1].Trim()
    }
  }
  if ($types.Count -eq 0) { return 'unparsed' }
  return ($types -join ' + ')
}

function Get-ReportedMemoryMiB([string[]] $lines) {
  $maximumBytes = 0L
  foreach ($line in $lines) {
    if ($line -match 'used\s+\d+\s+MB\s+\((\d+)\s+bytes\)') {
      $value = [int64]$Matches[1]
      if ($value -gt $maximumBytes) { $maximumBytes = $value }
    }
  }
  if ($maximumBytes -eq 0) { return $null }
  return [math]::Round($maximumBytes / 1MB, 1)
}

function Invoke-CapturedProcess(
  [string] $executablePath,
  [string[]] $arguments,
  [string] $workingDirectory
) {
  $startInfo = New-Object System.Diagnostics.ProcessStartInfo
  $startInfo.FileName = $executablePath
  $startInfo.Arguments = $arguments -join ' '
  $startInfo.WorkingDirectory = $workingDirectory
  $startInfo.UseShellExecute = $false
  $startInfo.CreateNoWindow = $true
  $startInfo.RedirectStandardOutput = $true
  $startInfo.RedirectStandardError = $true

  $process = New-Object System.Diagnostics.Process
  $process.StartInfo = $startInfo
  $timer = [System.Diagnostics.Stopwatch]::StartNew()
  if (-not $process.Start()) { throw "Unable to start $executablePath" }
  $standardOutput = $process.StandardOutput.ReadToEnd()
  $standardError = $process.StandardError.ReadToEnd()
  $process.WaitForExit()
  $timer.Stop()

  $combinedLines = @()
  if ($standardOutput.Length -gt 0) { $combinedLines += $standardOutput -split '\r?\n' }
  if ($standardError.Length -gt 0) { $combinedLines += $standardError -split '\r?\n' }
  return [pscustomobject]@{
    Lines = [string[]]$combinedLines
    ExitCode = $process.ExitCode
    Seconds = $timer.Elapsed.TotalSeconds
  }
}

function Invoke-CodecRoundTrip(
  [string] $label,
  [string] $codecPath,
  [string] $fileName,
  [string] $archivePath,
  [string] $restoredPath,
  [string] $compressLogPath,
  [string] $decompressLogPath
) {
  $sourcePath = Join-Path $fixtureDirectory $fileName
  foreach ($outputPath in @($archivePath, $restoredPath, $compressLogPath, $decompressLogPath)) {
    if (Test-Path -LiteralPath $outputPath) {
      throw "Refusing to overwrite an existing artifact: $outputPath"
    }
  }

  $compressRun = Invoke-CapturedProcess $codecPath @('-1', $fileName, $archivePath) $fixtureDirectory
  $compressLines = $compressRun.Lines
  $compressExitCode = $compressRun.ExitCode
  $compressSeconds = $compressRun.Seconds
  [System.IO.File]::WriteAllLines($compressLogPath, [string[]]$compressLines)
  if ($compressExitCode -ne 0) {
    throw "$label compression failed for $fileName with exit code $compressExitCode"
  }

  $decompressRun = Invoke-CapturedProcess $codecPath @('-d', $archivePath, $restoredPath) $benchmarkRoot
  $decompressLines = $decompressRun.Lines
  $decompressExitCode = $decompressRun.ExitCode
  $decompressSeconds = $decompressRun.Seconds
  [System.IO.File]::WriteAllLines($decompressLogPath, [string[]]$decompressLines)
  if ($decompressExitCode -ne 0) {
    throw "$label decompression failed for $fileName with exit code $decompressExitCode"
  }

  $sourceBytes = [System.IO.File]::ReadAllBytes($sourcePath)
  $restoredBytes = [System.IO.File]::ReadAllBytes($restoredPath)
  $firstDifference = -1
  if ($sourceBytes.Length -ne $restoredBytes.Length) {
    $firstDifference = [math]::Min($sourceBytes.Length, $restoredBytes.Length)
  }
  else {
    for ($index = 0; $index -lt $sourceBytes.Length; $index++) {
      if ($sourceBytes[$index] -ne $restoredBytes[$index]) {
        $firstDifference = $index
        break
      }
    }
  }

  $sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
  $restoredHash = (Get-FileHash -LiteralPath $restoredPath -Algorithm SHA256).Hash
  $exactEqual = $sourceBytes.Length -eq $restoredBytes.Length -and
                $firstDifference -eq -1 -and $sourceHash -eq $restoredHash
  if (-not $exactEqual) {
    throw "$label lossless verification failed for $fileName at offset $firstDifference"
  }

  return [pscustomobject]@{
    ArchiveBytes = (Get-Item -LiteralPath $archivePath).Length
    CompressSeconds = [math]::Round($compressSeconds, 3)
    DecompressSeconds = [math]::Round($decompressSeconds, 3)
    ReportedMemoryMiB = Get-ReportedMemoryMiB ($compressLines + $decompressLines)
    BlockTypes = Get-BlockTypes $compressLines
    CompressExit = $compressExitCode
    DecompressExit = $decompressExitCode
    ExactEqual = $exactEqual
    FirstDifference = $firstDifference
    SourceSha256 = $sourceHash
    RestoredSha256 = $restoredHash
  }
}

foreach ($directory in @(
  (Join-Path $archiveDirectory 'original'), (Join-Path $archiveDirectory 'derived'),
  (Join-Path $restoredDirectory 'original'), (Join-Path $restoredDirectory 'derived'),
  (Join-Path $logDirectory 'original'), (Join-Path $logDirectory 'derived')
)) {
  New-Item -ItemType Directory -Force -Path $directory | Out-Null
}

if ((Get-FileHash -LiteralPath $originalCodecPath -Algorithm SHA256).Hash -ne $expectedOriginalHash) {
  throw 'Original codec hash does not match the screenshot-matching v216 binary.'
}
if ((Get-FileHash -LiteralPath $derivedCodecPath -Algorithm SHA256).Hash -ne $expectedDerivedHash) {
  throw 'Derived codec hash changed after the experiment was planned.'
}
if (Test-Path -LiteralPath $resultPath) {
  throw "Refusing to overwrite an existing result: $resultPath"
}

$results = @()
foreach ($fileName in $fileNames) {
  $sourcePath = Join-Path $fixtureDirectory $fileName
  if ((Get-Item -LiteralPath $sourcePath).Length -ne 32768) {
    throw "Fixture is not exactly 32768 bytes: $sourcePath"
  }

  $original = Invoke-CodecRoundTrip `
    'original' $originalCodecPath $fileName `
    (Join-Path $archiveDirectory "original\$fileName.paq8px216") `
    (Join-Path $restoredDirectory "original\$fileName") `
    (Join-Path $logDirectory "original\$fileName.compress.log") `
    (Join-Path $logDirectory "original\$fileName.decompress.log")

  $derived = Invoke-CodecRoundTrip `
    'derived' $derivedCodecPath $fileName `
    (Join-Path $archiveDirectory "derived\$fileName.paq8pxsd217") `
    (Join-Path $restoredDirectory "derived\$fileName") `
    (Join-Path $logDirectory "derived\$fileName.compress.log") `
    (Join-Path $logDirectory "derived\$fileName.decompress.log")

  # paq8pxsd uses an 8-byte magic versus the original 6-byte magic. Subtracting
  # two bytes exposes the payload/model delta while raw bytes remain the actual archive size.
  $normalizedDerivedBytes = $derived.ArchiveBytes - 2
  $originalBpb = $original.ArchiveBytes * 8.0 / 32768
  $derivedBpb = $derived.ArchiveBytes * 8.0 / 32768
  $normalizedDerivedBpb = $normalizedDerivedBytes * 8.0 / 32768

  $results += [pscustomobject]@{
    File = $fileName
    RawBytes = 32768
    OriginalBytes = $original.ArchiveBytes
    DerivedBytes = $derived.ArchiveBytes
    RawDeltaBytes = $derived.ArchiveBytes - $original.ArchiveBytes
    NormalizedDerivedBytes = $normalizedDerivedBytes
    NormalizedDeltaBytes = $normalizedDerivedBytes - $original.ArchiveBytes
    RawDeltaPercent = [math]::Round((($derived.ArchiveBytes / [double]$original.ArchiveBytes) - 1.0) * 100.0, 4)
    NormalizedDeltaPercent = [math]::Round((($normalizedDerivedBytes / [double]$original.ArchiveBytes) - 1.0) * 100.0, 4)
    OriginalBpb = [math]::Round($originalBpb, 6)
    DerivedBpb = [math]::Round($derivedBpb, 6)
    RawDeltaBpb = [math]::Round($derivedBpb - $originalBpb, 6)
    NormalizedDerivedBpb = [math]::Round($normalizedDerivedBpb, 6)
    NormalizedDeltaBpb = [math]::Round($normalizedDerivedBpb - $originalBpb, 6)
    OriginalCompressSeconds = $original.CompressSeconds
    DerivedCompressSeconds = $derived.CompressSeconds
    OriginalDecompressSeconds = $original.DecompressSeconds
    DerivedDecompressSeconds = $derived.DecompressSeconds
    OriginalMemoryMiB = $original.ReportedMemoryMiB
    DerivedMemoryMiB = $derived.ReportedMemoryMiB
    OriginalBlockTypes = $original.BlockTypes
    DerivedBlockTypes = $derived.BlockTypes
    OriginalExactEqual = $original.ExactEqual
    DerivedExactEqual = $derived.ExactEqual
    OriginalFirstDifference = $original.FirstDifference
    DerivedFirstDifference = $derived.FirstDifference
    SourceSha256 = $derived.SourceSha256
  }

  Write-Output ("PASS {0}: original={1}, derived={2}, rawDelta={3:+#;-#;0}, normalizedDelta={4:+#;-#;0}, blocks={5} -> {6}" -f
    $fileName, $original.ArchiveBytes, $derived.ArchiveBytes,
    ($derived.ArchiveBytes - $original.ArchiveBytes),
    ($normalizedDerivedBytes - $original.ArchiveBytes),
    $original.BlockTypes, $derived.BlockTypes)
}

$results | Export-Csv -LiteralPath $resultPath -NoTypeInformation -Encoding utf8
Write-Output "RESULT_CSV=$resultPath"
