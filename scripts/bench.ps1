param(
  [string]$Program = "build/windows-ninja/tests/link.kr16",
  [int]$Iterations = 5
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
  if (-not (Test-Path $Program)) {
    Write-Error "program not found: $Program (run ctest first)"
  }
  $sw = [System.Diagnostics.Stopwatch]::StartNew()
  for ($i = 0; $i -lt $Iterations; $i++) {
    & cmd.exe /d /s /c "build\windows-ninja\kr16.exe vm `"$Program`" --stats >nul 2>&1"
  }
  $sw.Stop()
  $ms = $sw.Elapsed.TotalMilliseconds / $Iterations
  Write-Host ("{0}  {1:N2} ms/run" -f $Program, $ms)
} finally {
  Pop-Location
}
