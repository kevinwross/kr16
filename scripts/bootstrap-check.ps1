param(
  [switch]$Update
)

$ErrorActionPreference = "Stop"

function Get-ResourceHash([string]$Path) {
  $sha = [System.Security.Cryptography.SHA256]::Create()
  try {
    $text = [System.Text.Encoding]::UTF8.GetString(
      [System.IO.File]::ReadAllBytes($Path)).Replace("`r`n", "`n")
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($text)
    return ([System.BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToUpperInvariant()
  } finally {
    $sha.Dispose()
  }
}

$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
  $manifestPath = "resources/manifest.json"
  $manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
  $failed = $false
  foreach ($resource in $manifest.resources) {
    $actual = Get-ResourceHash $resource.source
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
