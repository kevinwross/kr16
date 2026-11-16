param(
  [string]$InstallDir = (Join-Path $env:LOCALAPPDATA "Programs\KR16"),
  [switch]$Uninstall,
  [switch]$NoPath,
  [switch]$NoShortcut
)

$ErrorActionPreference = "Stop"

$source = $PSScriptRoot
$binDir = Join-Path $InstallDir "bin"
$exe = Join-Path $binDir "kr16.exe"
$startMenu = [Environment]::GetFolderPath("Programs")
$shortcut = Join-Path $startMenu "KR16.lnk"
$pathEntry = $binDir

function Remove-UserPathEntry([string]$entry) {
  $current = [Environment]::GetEnvironmentVariable("Path", "User")
  if ([string]::IsNullOrWhiteSpace($current)) { return }
  $parts = $current.Split(";") | Where-Object {
    -not [string]::IsNullOrWhiteSpace($_) -and
    $_.TrimEnd("\") -ne $entry.TrimEnd("\")
  }
  [Environment]::SetEnvironmentVariable("Path", ($parts -join ";"), "User")
}

if ($Uninstall) {
  if (Test-Path $shortcut) { Remove-Item -Force $shortcut }
  Remove-UserPathEntry $pathEntry
  if (Test-Path $InstallDir) {
    Remove-Item -Recurse -Force $InstallDir
  }
  Write-Host "KR16 uninstalled from $InstallDir"
  exit 0
}

$sourceExe = Join-Path (Join-Path $source "bin") "kr16.exe"
if (-not (Test-Path $sourceExe)) {
  throw "install.ps1 must run from an extracted KR16 package (missing bin\kr16.exe)"
}

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
Copy-Item (Join-Path $source "bin") $InstallDir -Recurse -Force
foreach ($item in @("resources", "docs", "vscode")) {
  $from = Join-Path $source $item
  if (Test-Path $from) {
    Copy-Item $from $InstallDir -Recurse -Force
  }
}
foreach ($file in @(
    "README.md",
    "LICENSE",
    "THIRD_PARTY_NOTICES.md",
    "RELEASE_NOTES-1.0.0.md",
    "install.ps1",
    "uninstall.ps1")) {
  $from = Join-Path $source $file
  if (Test-Path $from) {
    Copy-Item $from $InstallDir -Force
  }
}

if (-not $NoShortcut) {
  $shell = New-Object -ComObject WScript.Shell
  $link = $shell.CreateShortcut($shortcut)
  $link.TargetPath = $exe
  $link.WorkingDirectory = $InstallDir
  $link.Description = "KR16 teaching computer"
  $link.Save()
}

if (-not $NoPath) {
  $current = [Environment]::GetEnvironmentVariable("Path", "User")
  $present = $false
  foreach ($part in ($current -split ";")) {
    if ($part.TrimEnd("\") -eq $pathEntry.TrimEnd("\")) { $present = $true }
  }
  if (-not $present) {
    $updated = if ([string]::IsNullOrWhiteSpace($current)) {
      $pathEntry
    } else {
      "$current;$pathEntry"
    }
    [Environment]::SetEnvironmentVariable("Path", $updated, "User")
  }
}

Write-Host "Installed KR16 to $InstallDir"
Write-Host "Run: $exe"
if (-not $NoPath) {
  Write-Host "Open a new terminal to use kr16 from PATH."
}
