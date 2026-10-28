param(
  [switch]$Update
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
  $manifestPath = "resources/manifest.json"
  $manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
  $failed = $false
  foreach ($resource in $manifest.resources) {
    $actual = (Get-FileHash $resource.source -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($Update) {
      $resource.sha256 = $actual
    } elseif ($actual -ne $resource.sha256) {
      Write-Error "bootstrap drift: $($resource.source) expected $($resource.sha256), got $actual"
      $failed = $true
    }
  }
  if ($Update) {
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -Encoding ascii $manifestPath
    Write-Host "updated $manifestPath"
  }
  if ($failed) { exit 1 }
  Write-Host "resource hashes match the manifest"
} finally {
  Pop-Location
}
