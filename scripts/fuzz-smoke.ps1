param(
  [int]$Iterations = 50
)

$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
  $bin = "build/windows-ninja/kr16.exe"
  if (-not (Test-Path $bin)) { Write-Error "missing $bin"; exit 1 }
  $tmp = "build/fuzz-smoke.bin"
  $rng = [System.Random]::new(12345)
  for ($i = 0; $i -lt $Iterations; $i++) {
    $length = $rng.Next(0, 512)
    $bytes = New-Object byte[] $length
    $rng.NextBytes($bytes)
    [IO.File]::WriteAllBytes($tmp, $bytes)
    & cmd.exe /d /s /c "`"$bin`" dis `"$tmp`" --raw >nul 2>&1" | Out-Null
    if ($LASTEXITCODE -gt 1) { Write-Error "dis crashed on iteration $i"; exit 1 }
    & cmd.exe /d /s /c "`"$bin`" debug `"$tmp`" >nul 2>&1" | Out-Null
    if ($LASTEXITCODE -gt 1) { Write-Error "debug crashed on iteration $i"; exit 1 }
  }
  Remove-Item $tmp -ErrorAction SilentlyContinue
  Write-Host "fuzz smoke passed ($Iterations iterations)"
} finally {
  Pop-Location
}
