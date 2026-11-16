param(
  [string]$InstallDir = (Join-Path $env:LOCALAPPDATA "Programs\KR16")
)

$ErrorActionPreference = "Stop"
& (Join-Path $PSScriptRoot "install.ps1") -InstallDir $InstallDir -Uninstall
exit $LASTEXITCODE
