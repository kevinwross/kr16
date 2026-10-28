param(
  [string]$Version = "0.1.0",
  [string]$Configuration = "windows-ninja-release"
)

$ErrorActionPreference = "Stop"
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
  Copy-Item README.md, LICENSE, THIRD_PARTY_NOTICES.md $stage
  Copy-Item docs, examples, resources, vscode $stage -Recurse
  $zip = Join-Path $dist "kr16-$Version-windows-x64.zip"
  if (Test-Path $zip) { Remove-Item -Force $zip }
  Compress-Archive -Path $stage -DestinationPath $zip
  $hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
  "$hash  $(Split-Path -Leaf $zip)" | Set-Content -Encoding ascii (Join-Path $dist "SHA256SUMS")
  Write-Host "wrote $zip"
} finally {
  Pop-Location
}
