param(
  [Parameter(Mandatory = $true)][string]$SourceExe,
  [Parameter(Mandatory = $true)][string]$InstallScript,
  [Parameter(Mandatory = $true)][string]$UninstallScript
)

$ErrorActionPreference = "Stop"
$stage = Join-Path $env:TEMP "kr16-installer-stage"
$target = Join-Path $env:TEMP "kr16-installer-target"
Remove-Item -Recurse -Force $stage, $target -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path (Join-Path $stage "bin") | Out-Null
Copy-Item $SourceExe (Join-Path $stage "bin/kr16.exe")
Copy-Item $InstallScript (Join-Path $stage "install.ps1")
Copy-Item $UninstallScript (Join-Path $stage "uninstall.ps1")

try {
  & (Join-Path $stage "install.ps1") -InstallDir $target -NoPath -NoShortcut
  $installed = Join-Path $target "bin/kr16.exe"
  if (-not (Test-Path $installed)) { throw "installer did not copy kr16.exe" }
  & $installed version | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "installed kr16 did not run" }

  & (Join-Path $stage "uninstall.ps1") -InstallDir $target
  if (Test-Path $target) { throw "uninstaller left $target behind" }
  Write-Host "installer test passed"
} finally {
  Remove-Item -Recurse -Force $stage, $target -ErrorAction SilentlyContinue
}

