[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string] $CandidateCodecPath,

  [Parameter(Mandatory = $true)]
  [ValidatePattern('^[0-9A-Fa-f]{64}$')]
  [string] $ExpectedCandidateSha256,

  [string] $OriginalCodecPath = 'F:\paq8px\PaqBenchStudio\staging-v1.1.0\paq8px.exe',

  [ValidatePattern('^[0-9A-Fa-f]{64}$')]
  [string] $ExpectedOriginalSha256 = 'F79343702F596A4FA6C7CC3E25F2FA9C05199EAF11F06655B065F687E5F42533'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$benchmarkRoot = [System.IO.Path]::GetFullPath((Split-Path -Parent $MyInvocation.MyCommand.Path))
$fixtureDirectory = [System.IO.Path]::GetFullPath((Join-Path $benchmarkRoot '..\EXP01-paq8pxsd-v217-32KiB\inputs'))
$archiveDirectory = Join-Path $benchmarkRoot 'archives'
$restoredDirectory = Join-Path $benchmarkRoot 'restored'
$logDirectory = Join-Path $benchmarkRoot 'logs'
$resultPath = Join-Path $benchmarkRoot 'results.csv'

$fileNames = @(
  'dickens', 'mozilla', 'mr', 'nci', 'ooffice', 'osdb',
  'reymont', 'samba', 'sao', 'webster', 'x-ray', 'xml'
)

$expectedInputSha256 = @{
  'dickens' = 'FC42DCB9849222C8704C9DCAE606D075B389B66244FB215035148D6409EC0B31'
  'mozilla' = '9DDEEF36CA0CA55B72FE3376D005926DFF3400A2ADE6EAE18482D8017D8645DB'
  'mr'      = '3BB287B0AF65F777AB00C14E362B4D1962087260556001BBEE0689AA10D9F76A'
  'nci'     = '0D3034FE8B0E573DE1439ED98CE409B83B13E5895085C9BFB0BF980F4962FB79'
  'ooffice' = '2ACF9B4CAAEAC5814CDFEF0FA48A5ECE857C847DBDB7B44EEAB95CA3C098921C'
  'osdb'    = '03B2C20B777682CD960BCD893D7A4463161D9C0200FD1708B2EFAE32A259D0B7'
  'reymont' = '9D5E4B9340C2260DAE7DDB01B3EDA58236FF2DE5E1FF4D56767AA840B6FB1A87'
  'samba'   = '9C4F5BEE544E4531E4946E62F796987418B4526911C34877EF282F14DD57AD12'
  'sao'     = '95677430CAD4F000506BC5EE22815C3F4E13D64B477ED506B78D5D9ACFB50CCD'
  'webster' = '774D224695AF4057353F72602BB96CD2AFEEA059AC7737535645F11596FCA85E'
  'x-ray'   = '10511AA63DFBD9C0DDBFEFE36068740103D4BA1116E214154A0772057D7E9314'
  'xml'     = 'C2F7F129956F8D6FC3D0E3595D93F98D5270B6F5BC4AB5E70F45867822EFDD71'
}

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

function Resolve-ExistingFile([string] $path, [string] $description) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "$description does not exist or is not a file: $path"
  }
  return (Resolve-Path -LiteralPath $path).ProviderPath
}

function Assert-PathWithinRoot([string] $path, [string] $root) {
  $fullPath = [System.IO.Path]::GetFullPath($path)
  $rootPrefix = [System.IO.Path]::GetFullPath($root).TrimEnd('\', '/') +
    [System.IO.Path]::DirectorySeparatorChar
  if (-not $fullPath.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Output path escapes the benchmark directory: $fullPath"
  }
}

function Assert-SafeOutputDirectory([string] $path) {
  Assert-PathWithinRoot $path $benchmarkRoot
  $rootPath = [System.IO.Path]::GetFullPath($benchmarkRoot).TrimEnd('\', '/')
  $ancestorPath = [System.IO.Path]::GetFullPath($path)
  while ($ancestorPath.StartsWith($rootPath, [System.StringComparison]::OrdinalIgnoreCase)) {
    if (Test-Path -LiteralPath $ancestorPath) {
      $ancestor = Get-Item -LiteralPath $ancestorPath -Force
      if (($ancestor.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Refusing to write through a reparse-point path: $ancestorPath"
      }
    }
    if ($ancestorPath -eq $rootPath) { break }
    $ancestorPath = Split-Path -Parent $ancestorPath
  }
  if (Test-Path -LiteralPath $path) {
    $item = Get-Item -LiteralPath $path -Force
    if (-not $item.PSIsContainer) {
      throw "Expected an output directory but found a file: $path"
    }
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
      throw "Refusing to write through a reparse-point output directory: $path"
    }
  }
}

function ConvertTo-NativeArgument([string] $argument) {
  if ($null -eq $argument) {
    throw 'A native process argument is null.'
  }
  if ($argument.IndexOf([char]0) -ge 0) {
    throw 'A native process argument contains a null character.'
  }

  # Quote according to the Windows CommandLineToArgvW rules. This keeps paths
  # containing whitespace, quotes, or trailing backslashes as one argument.
  $builder = New-Object System.Text.StringBuilder
  [void]$builder.Append([char]34)
  $index = 0
  while ($index -lt $argument.Length) {
    $backslashCount = 0
    while ($index -lt $argument.Length -and $argument[$index] -eq [char]92) {
      $backslashCount++
      $index++
    }

    if ($index -eq $argument.Length) {
      for ($count = 0; $count -lt (2 * $backslashCount); $count++) {
        [void]$builder.Append([char]92)
      }
      break
    }

    if ($argument[$index] -eq [char]34) {
      for ($count = 0; $count -lt (2 * $backslashCount + 1); $count++) {
        [void]$builder.Append([char]92)
      }
      [void]$builder.Append([char]34)
    }
    else {
      for ($count = 0; $count -lt $backslashCount; $count++) {
        [void]$builder.Append([char]92)
      }
      [void]$builder.Append($argument[$index])
    }
    $index++
  }
  [void]$builder.Append([char]34)
  return $builder.ToString()
}

function Invoke-CapturedProcess(
  [string] $executablePath,
  [string[]] $arguments,
  [string] $workingDirectory
) {
  $startInfo = New-Object System.Diagnostics.ProcessStartInfo
  $startInfo.FileName = $executablePath
  $startInfo.Arguments = (($arguments | ForEach-Object { ConvertTo-NativeArgument $_ }) -join ' ')
  $startInfo.WorkingDirectory = $workingDirectory
  $startInfo.UseShellExecute = $false
  $startInfo.CreateNoWindow = $true
  $startInfo.RedirectStandardOutput = $true
  $startInfo.RedirectStandardError = $true

  $process = New-Object System.Diagnostics.Process
  $process.StartInfo = $startInfo
  $timer = [System.Diagnostics.Stopwatch]::StartNew()
  try {
    if (-not $process.Start()) {
      throw "Unable to start executable: $executablePath"
    }
    $standardOutputTask = $process.StandardOutput.ReadToEndAsync()
    $standardErrorTask = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $standardOutput = $standardOutputTask.Result
    $standardError = $standardErrorTask.Result
    $timer.Stop()
    $process.Refresh()

    return [pscustomobject]@{
      StandardOutput = $standardOutput
      StandardError = $standardError
      ExitCode = $process.ExitCode
      Seconds = $timer.Elapsed.TotalSeconds
      PeakWorkingSetBytes = [int64]$process.PeakWorkingSet64
    }
  }
  finally {
    if ($timer.IsRunning) { $timer.Stop() }
    $process.Dispose()
  }
}

function Get-OutputLines([string] $standardOutput, [string] $standardError) {
  $lines = @()
  if ($standardOutput.Length -gt 0) { $lines += $standardOutput -split '\r?\n' }
  if ($standardError.Length -gt 0) { $lines += $standardError -split '\r?\n' }
  return [string[]]$lines
}

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

function Get-RoutedSegmentStats([string[]] $lines) {
  foreach ($line in $lines) {
    if ($line -match 'Routed segments\s*:\s*(\d+)\s*\(PAQ\s+(\d+),\s*external\s+(\d+)\)') {
      $total = [uint32]$Matches[1]
      $paq = [uint32]$Matches[2]
      $external = [uint32]$Matches[3]
      if ($total -eq 0 -or $total -ne ($paq + $external)) {
        throw "Candidate reported inconsistent routed segment counts: $line"
      }
      return [pscustomobject]@{
        Total = $total
        Paq = $paq
        External = $external
      }
    }
  }
  throw 'Candidate compression output did not contain a parseable Routed segments line.'
}

function Assert-RoutedArchiveMagic([string] $archivePath) {
  $expectedMagic = [byte[]](80, 65, 81, 88, 82, 80, 50, 10)
  $archiveBytes = [System.IO.File]::ReadAllBytes($archivePath)
  if ($archiveBytes.Length -lt $expectedMagic.Length) {
    throw "Candidate archive is shorter than the PAQXRP2 magic: $archivePath"
  }
  for ($index = 0; $index -lt $expectedMagic.Length; $index++) {
    if ($archiveBytes[$index] -ne $expectedMagic[$index]) {
      throw "Candidate archive does not start with PAQXRP2\n: $archivePath"
    }
  }
}

function Get-RoundTripPaths(
  [string] $label,
  [string] $fileName,
  [string] $archiveExtension
) {
  return [pscustomobject]@{
    Archive = Join-Path $archiveDirectory "$label\$fileName.$archiveExtension"
    Restored = Join-Path $restoredDirectory "$label\$fileName"
    CompressStdout = Join-Path $logDirectory "$label\$fileName.compress.stdout.log"
    CompressStderr = Join-Path $logDirectory "$label\$fileName.compress.stderr.log"
    DecompressStdout = Join-Path $logDirectory "$label\$fileName.decompress.stdout.log"
    DecompressStderr = Join-Path $logDirectory "$label\$fileName.decompress.stderr.log"
  }
}

function Write-ProcessLogs([pscustomobject] $run, [string] $stdoutPath, [string] $stderrPath) {
  foreach ($entry in @(
    [pscustomobject]@{ Path = $stdoutPath; Content = $run.StandardOutput },
    [pscustomobject]@{ Path = $stderrPath; Content = $run.StandardError }
  )) {
    $stream = $null
    $writer = $null
    try {
      $stream = [System.IO.File]::Open(
        $entry.Path,
        [System.IO.FileMode]::CreateNew,
        [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::Read
      )
      $writer = New-Object System.IO.StreamWriter($stream, $utf8NoBom)
      $writer.Write($entry.Content)
      $writer.Flush()
    }
    finally {
      if ($null -ne $writer) { $writer.Dispose() }
      elseif ($null -ne $stream) { $stream.Dispose() }
    }
  }
}

function Invoke-CodecRoundTrip(
  [string] $label,
  [string] $codecPath,
  [string] $fileName,
  [pscustomobject] $paths,
  [bool] $requireRoutedArchive
) {
  $sourcePath = Join-Path $fixtureDirectory $fileName
  foreach ($outputPath in @(
    $paths.Archive, $paths.Restored,
    $paths.CompressStdout, $paths.CompressStderr,
    $paths.DecompressStdout, $paths.DecompressStderr
  )) {
    if (Test-Path -LiteralPath $outputPath) {
      throw "Refusing to overwrite an existing artifact: $outputPath"
    }
  }

  $compressRun = Invoke-CapturedProcess $codecPath @('-1', $fileName, $paths.Archive) $fixtureDirectory
  Write-ProcessLogs $compressRun $paths.CompressStdout $paths.CompressStderr
  if ($compressRun.ExitCode -ne 0) {
    throw "$label compression failed for $fileName with exit code $($compressRun.ExitCode)"
  }
  if (-not (Test-Path -LiteralPath $paths.Archive -PathType Leaf)) {
    throw "$label compression produced no archive for $fileName"
  }

  [string[]]$compressLines = @(Get-OutputLines $compressRun.StandardOutput $compressRun.StandardError)
  $routedStats = $null
  if ($requireRoutedArchive) {
    Assert-RoutedArchiveMagic $paths.Archive
    $routedStats = Get-RoutedSegmentStats $compressLines
  }

  $decompressRun = Invoke-CapturedProcess $codecPath @('-d', $paths.Archive, $paths.Restored) $benchmarkRoot
  Write-ProcessLogs $decompressRun $paths.DecompressStdout $paths.DecompressStderr
  if ($decompressRun.ExitCode -ne 0) {
    throw "$label decompression failed for $fileName with exit code $($decompressRun.ExitCode)"
  }
  if (-not (Test-Path -LiteralPath $paths.Restored -PathType Leaf)) {
    throw "$label decompression produced no restored file for $fileName"
  }

  $sourceBytes = [System.IO.File]::ReadAllBytes($sourcePath)
  $restoredBytes = [System.IO.File]::ReadAllBytes($paths.Restored)
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
  $restoredHash = (Get-FileHash -LiteralPath $paths.Restored -Algorithm SHA256).Hash
  if ($sourceHash -ne $expectedInputSha256[$fileName]) {
    throw "Locked fixture changed during the run: $sourcePath"
  }
  $exactEqual = $sourceBytes.Length -eq $restoredBytes.Length -and
                $firstDifference -eq -1 -and
                $sourceHash -eq $restoredHash
  if (-not $exactEqual) {
    throw "$label lossless verification failed for $fileName at offset $firstDifference"
  }

  [string[]]$allLines = @($compressLines) +
    @(Get-OutputLines $decompressRun.StandardOutput $decompressRun.StandardError)
  return [pscustomobject]@{
    ArchiveBytes = (Get-Item -LiteralPath $paths.Archive).Length
    ArchiveSha256 = (Get-FileHash -LiteralPath $paths.Archive -Algorithm SHA256).Hash
    CompressSeconds = [math]::Round($compressRun.Seconds, 3)
    DecompressSeconds = [math]::Round($decompressRun.Seconds, 3)
    CompressPeakMemoryMiB = [math]::Round($compressRun.PeakWorkingSetBytes / 1MB, 1)
    DecompressPeakMemoryMiB = [math]::Round($decompressRun.PeakWorkingSetBytes / 1MB, 1)
    ReportedMemoryMiB = Get-ReportedMemoryMiB $allLines
    BlockTypes = Get-BlockTypes $compressLines
    CompressExit = $compressRun.ExitCode
    DecompressExit = $decompressRun.ExitCode
    ExactEqual = $exactEqual
    FirstDifference = $firstDifference
    SourceSha256 = $sourceHash
    RestoredSha256 = $restoredHash
    RoutedSegments = if ($null -eq $routedStats) { $null } else { $routedStats.Total }
    PaqSegments = if ($null -eq $routedStats) { $null } else { $routedStats.Paq }
    ExternalSegments = if ($null -eq $routedStats) { $null } else { $routedStats.External }
  }
}

# Resolve and lock both executables before creating any output directory.
$resolvedOriginalCodecPath = Resolve-ExistingFile $OriginalCodecPath 'Original codec'
$resolvedCandidateCodecPath = Resolve-ExistingFile $CandidateCodecPath 'Candidate codec'
$actualOriginalSha256 = (Get-FileHash -LiteralPath $resolvedOriginalCodecPath -Algorithm SHA256).Hash
$actualCandidateSha256 = (Get-FileHash -LiteralPath $resolvedCandidateCodecPath -Algorithm SHA256).Hash
if ($actualOriginalSha256 -ne $ExpectedOriginalSha256.ToUpperInvariant()) {
  throw "Original codec SHA-256 mismatch. Expected $ExpectedOriginalSha256, got $actualOriginalSha256"
}
if ($actualCandidateSha256 -ne $ExpectedCandidateSha256.ToUpperInvariant()) {
  throw "Candidate codec SHA-256 mismatch. Expected $ExpectedCandidateSha256, got $actualCandidateSha256"
}
if ($resolvedOriginalCodecPath -eq $resolvedCandidateCodecPath -or
    $actualOriginalSha256 -eq $actualCandidateSha256) {
  throw 'Original and candidate codecs must be distinct locked executables.'
}

if (-not (Test-Path -LiteralPath $fixtureDirectory -PathType Container)) {
  throw "Locked EXP01 fixture directory is missing: $fixtureDirectory"
}

# Verify all inputs and all destination paths before starting the first codec.
$plannedArtifacts = New-Object System.Collections.Generic.List[string]
foreach ($fileName in $fileNames) {
  $sourcePath = Join-Path $fixtureDirectory $fileName
  if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
    throw "Missing locked fixture: $sourcePath"
  }
  $sourceItem = Get-Item -LiteralPath $sourcePath
  if ($sourceItem.Length -ne 32768) {
    throw "Fixture is not exactly 32768 bytes: $sourcePath"
  }
  $sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
  if ($sourceHash -ne $expectedInputSha256[$fileName]) {
    throw "Fixture SHA-256 mismatch for $fileName. Expected $($expectedInputSha256[$fileName]), got $sourceHash"
  }

  foreach ($paths in @(
    (Get-RoundTripPaths 'original' $fileName 'paq8px216'),
    (Get-RoundTripPaths 'candidate' $fileName 'paq8pxhy219')
  )) {
    foreach ($outputPath in @(
      $paths.Archive, $paths.Restored,
      $paths.CompressStdout, $paths.CompressStderr,
      $paths.DecompressStdout, $paths.DecompressStderr
    )) {
      Assert-PathWithinRoot $outputPath $benchmarkRoot
      [void]$plannedArtifacts.Add($outputPath)
    }
  }
}
[void]$plannedArtifacts.Add($resultPath)
foreach ($artifactPath in $plannedArtifacts) {
  if (Test-Path -LiteralPath $artifactPath) {
    throw "Refusing to overwrite an existing benchmark artifact: $artifactPath"
  }
}

$outputDirectories = @(
  (Join-Path $archiveDirectory 'original'), (Join-Path $archiveDirectory 'candidate'),
  (Join-Path $restoredDirectory 'original'), (Join-Path $restoredDirectory 'candidate'),
  (Join-Path $logDirectory 'original'), (Join-Path $logDirectory 'candidate')
)
foreach ($directory in $outputDirectories) {
  Assert-SafeOutputDirectory $directory
  New-Item -ItemType Directory -Force -Path $directory | Out-Null
}

Write-Output "ORIGINAL_CODEC=$resolvedOriginalCodecPath"
Write-Output "ORIGINAL_SHA256=$actualOriginalSha256"
Write-Output "CANDIDATE_CODEC=$resolvedCandidateCodecPath"
Write-Output "CANDIDATE_SHA256=$actualCandidateSha256"
Write-Output "FIXTURES=$fixtureDirectory"
Write-Output 'PROTOCOL=level -1; offset 0; 32768 bytes; serial; one repetition'

$results = @()
foreach ($fileName in $fileNames) {
  Write-Output "START $fileName"
  $originalPaths = Get-RoundTripPaths 'original' $fileName 'paq8px216'
  $candidatePaths = Get-RoundTripPaths 'candidate' $fileName 'paq8pxhy219'

  $original = Invoke-CodecRoundTrip `
    'original' $resolvedOriginalCodecPath $fileName $originalPaths $false
  $candidate = Invoke-CodecRoundTrip `
    'candidate' $resolvedCandidateCodecPath $fileName $candidatePaths $true

  $rawBytes = 32768
  $originalBpb = $original.ArchiveBytes * 8.0 / $rawBytes
  $candidateBpb = $candidate.ArchiveBytes * 8.0 / $rawBytes
  $results += [pscustomobject]@{
    File = $fileName
    RawBytes = $rawBytes
    SourceSha256 = $original.SourceSha256
    OriginalCodecSha256 = $actualOriginalSha256
    CandidateCodecSha256 = $actualCandidateSha256
    OriginalArchiveBytes = $original.ArchiveBytes
    CandidateArchiveBytes = $candidate.ArchiveBytes
    CandidateMinusOriginalBytes = $candidate.ArchiveBytes - $original.ArchiveBytes
    CandidateMinusOriginalPercent = [math]::Round(
      (($candidate.ArchiveBytes / [double]$original.ArchiveBytes) - 1.0) * 100.0, 6)
    OriginalBpb = [math]::Round($originalBpb, 6)
    CandidateBpb = [math]::Round($candidateBpb, 6)
    CandidateMinusOriginalBpb = [math]::Round($candidateBpb - $originalBpb, 6)
    OriginalCompressSeconds = $original.CompressSeconds
    CandidateCompressSeconds = $candidate.CompressSeconds
    OriginalDecompressSeconds = $original.DecompressSeconds
    CandidateDecompressSeconds = $candidate.DecompressSeconds
    OriginalCompressPeakMemoryMiB = $original.CompressPeakMemoryMiB
    CandidateCompressPeakMemoryMiB = $candidate.CompressPeakMemoryMiB
    OriginalDecompressPeakMemoryMiB = $original.DecompressPeakMemoryMiB
    CandidateDecompressPeakMemoryMiB = $candidate.DecompressPeakMemoryMiB
    OriginalReportedMemoryMiB = $original.ReportedMemoryMiB
    CandidateReportedMemoryMiB = $candidate.ReportedMemoryMiB
    OriginalBlockTypes = $original.BlockTypes
    CandidateBlockTypes = $candidate.BlockTypes
    CandidateRoutedSegments = $candidate.RoutedSegments
    CandidatePaqSegments = $candidate.PaqSegments
    CandidateExternalSegments = $candidate.ExternalSegments
    CandidateMagicValid = $true
    OriginalCompressExit = $original.CompressExit
    OriginalDecompressExit = $original.DecompressExit
    CandidateCompressExit = $candidate.CompressExit
    CandidateDecompressExit = $candidate.DecompressExit
    OriginalExactEqual = $original.ExactEqual
    CandidateExactEqual = $candidate.ExactEqual
    OriginalFirstDifference = $original.FirstDifference
    CandidateFirstDifference = $candidate.FirstDifference
    OriginalArchiveSha256 = $original.ArchiveSha256
    CandidateArchiveSha256 = $candidate.ArchiveSha256
    OriginalRestoredSha256 = $original.RestoredSha256
    CandidateRestoredSha256 = $candidate.RestoredSha256
  }

  Write-Output ("PASS {0}: original={1}, candidate={2}, delta={3:+#;-#;0}, routed={4} (PAQ {5}, external {6})" -f
    $fileName, $original.ArchiveBytes, $candidate.ArchiveBytes,
    ($candidate.ArchiveBytes - $original.ArchiveBytes),
    $candidate.RoutedSegments, $candidate.PaqSegments, $candidate.ExternalSegments)
}

$csvLines = @($results | ConvertTo-Csv -NoTypeInformation)
$resultStream = $null
$resultWriter = $null
try {
  $resultStream = [System.IO.File]::Open(
    $resultPath,
    [System.IO.FileMode]::CreateNew,
    [System.IO.FileAccess]::Write,
    [System.IO.FileShare]::Read
  )
  $resultWriter = New-Object System.IO.StreamWriter($resultStream, $utf8NoBom)
  foreach ($csvLine in $csvLines) { $resultWriter.WriteLine($csvLine) }
  $resultWriter.Flush()
}
finally {
  if ($null -ne $resultWriter) { $resultWriter.Dispose() }
  elseif ($null -ne $resultStream) { $resultStream.Dispose() }
}
Write-Output "RESULT_CSV=$resultPath"
