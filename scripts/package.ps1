param(
  [string]$Version = "0.1.0",
  [string]$Configuration = "windows-ninja-release"
)

$ErrorActionPreference = "Stop"

function Get-Sha256([string]$Path) {
  $sha = [System.Security.Cryptography.SHA256]::Create()
  try {
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    return ([System.BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
  } finally {
    $sha.Dispose()
  }
}

$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
  cmake --preset $Configuration
  cmake --build --preset $Configuration
  ctest --preset $Configuration

  $dist = Join-Path $root "dist"
  $stage = Join-Path $dist "kr16-$Version-windows-x64"
  if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
  New-Item -ItemType Directory -Force -Path (Join-Path $stage "bin") | Out-Null
  Copy-Item "build/windows-ninja/kr16.exe" (Join-Path $stage "bin/kr16.exe")
  Copy-Item README.md, LICENSE, THIRD_PARTY_NOTICES.md, RELEASE_NOTES-0.1.0.md $stage
  Copy-Item docs, examples, resources $stage -Recurse
  $vscodeStage = Join-Path $stage "vscode"
  New-Item -ItemType Directory -Force -Path $vscodeStage | Out-Null
  Copy-Item "vscode/package.json", "vscode/extension.js", "vscode/README.md", "vscode/LICENSE" $vscodeStage
  $vsix = Join-Path $root "vscode/kr16-debug-$Version.vsix"
  if (Test-Path $vsix) {
    Copy-Item $vsix $stage
  }
  $zip = Join-Path $dist "kr16-$Version-windows-x64.zip"
  if (Test-Path $zip) { Remove-Item -Force $zip }
  Add-Type -AssemblyName System.IO.Compression
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $epochSeconds = 315532800  # 1980-01-01, the ZIP epoch
  if ($env:SOURCE_DATE_EPOCH) {
    [int64]$parsed = 0
    if ([int64]::TryParse($env:SOURCE_DATE_EPOCH, [ref]$parsed) -and $parsed -ge 0) {
      $epochSeconds = [Math]::Max([int64]315532800, $parsed)
    }
  }
  $epoch = [DateTimeOffset]::FromUnixTimeSeconds($epochSeconds).UtcDateTime
  $stream = [IO.File]::Open($zip, [IO.FileMode]::Create)
  $archive = [System.IO.Compression.ZipArchive]::new(
    $stream, [System.IO.Compression.ZipArchiveMode]::Create)
  try {
    $files = Get-ChildItem $stage -Recurse -File | Sort-Object FullName
    foreach ($file in $files) {
      $relative = $file.FullName.Substring($dist.Length + 1).Replace('\', '/')
      $entry = $archive.CreateEntry(
        $relative, [System.IO.Compression.CompressionLevel]::Optimal)
      $entry.LastWriteTime = $epoch
      $input = [IO.File]::OpenRead($file.FullName)
      $output = $entry.Open()
      try { $input.CopyTo($output) } finally { $output.Dispose(); $input.Dispose() }
    }
  } finally {
    $archive.Dispose()
    $stream.Dispose()
  }
  $hash = (Get-Sha256 $zip)
  "$hash  $(Split-Path -Leaf $zip)" | Set-Content -Encoding ascii (Join-Path $dist "SHA256SUMS")
  if (Test-Path $vsix) {
    $vsixHash = (Get-Sha256 $vsix)
    "$vsixHash  $(Split-Path -Leaf $vsix)" | Add-Content -Encoding ascii (Join-Path $dist "SHA256SUMS")
  }
  Write-Host "wrote $zip"
} finally {
  Pop-Location
}
