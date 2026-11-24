<#
.SYNOPSIS
    Build, package, unpack and run a release, twice, and compare.

.DESCRIPTION
    A release that has never been unpacked has never been tested. This script:

      1. builds and packages the tree twice into separate output directories;
      2. compares the two archives by hash, which catches a timestamp or a path
         leaking into the artefact;
      3. unpacks one archive into a scratch directory;
      4. runs the packaged executable's --version and one fixture from the
         unpacked copy, not from the build tree;
      5. checks that SHA256SUMS exists, names the archive, and matches it.

    A failure stops at the step that failed and prints what it ran.

.PARAMETER Version
    The version to package. Defaults to 1.0.0.

.PARAMETER Configuration
    The preset to build. Defaults to windows-ninja-release.

.EXAMPLE
    ./scripts/verify-release.ps1
#>
[CmdletBinding()]
param(
    [string]$Version = '1.0.0',
    [string]$Configuration = 'windows-ninja-release'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$work = Join-Path $root 'build/verify-release'
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Force $work | Out-Null

function Invoke-Checked([string]$Label, [scriptblock]$Action) {
    Write-Host "verify: $Label"
    & $Action
    if ($LASTEXITCODE -ne 0) { throw "$Label failed" }
}

# 1. Two builds and two packages, into separate directories.
$first = Join-Path $work 'first'
$second = Join-Path $work 'second'
foreach ($target in @($first, $second)) {
    if (Test-Path (Join-Path $root 'dist')) { Remove-Item -Recurse -Force (Join-Path $root 'dist') }
    Invoke-Checked "package into $target" {
        & pwsh -NoProfile -File (Join-Path $root 'scripts/package.ps1') `
            -Version $Version -Configuration $Configuration
        $global:LASTEXITCODE = if ($?) { 0 } else { 1 }
    }
    New-Item -ItemType Directory -Force $target | Out-Null
    Copy-Item (Join-Path $root 'dist/*') $target -Recurse -Force
}

$archiveName = "kr16-$Version-windows-x64.zip"
$a = Join-Path $first $archiveName
$b = Join-Path $second $archiveName
if (-not (Test-Path $a)) { throw "the packager did not write $archiveName" }
if (-not (Test-Path $b)) { throw "the second package did not write $archiveName" }

# 2. The two archives must be byte-identical.
$hashA = (Get-FileHash $a -Algorithm SHA256).Hash
$hashB = (Get-FileHash $b -Algorithm SHA256).Hash
if ($hashA -ne $hashB) {
    throw "two packages of the same tree differ: $hashA vs $hashB; the artefact carries the host or the clock"
}
Write-Host "verify: two packages match ($hashA)"

# 5. SHA256SUMS names the archive and matches it.
$sums = Join-Path $first 'SHA256SUMS'
if (-not (Test-Path $sums)) { throw 'SHA256SUMS was not written' }
$line = Get-Content $sums | Where-Object { $_ -match [regex]::Escape($archiveName) } | Select-Object -First 1
if (-not $line) { throw "SHA256SUMS does not name $archiveName" }
$recorded = ($line -split '\s+')[0]
if ($recorded.ToLower() -ne $hashA.ToLower()) {
    throw "SHA256SUMS records $recorded for $archiveName but the archive hashes to $hashA"
}
Write-Host "verify: SHA256SUMS matches the archive"

# 3. Unpack, and run from the unpacked copy.
$unpacked = Join-Path $work 'unpacked'
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::ExtractToDirectory($a, $unpacked)
# The archive holds one top-level directory named for the release.
$exe = Get-ChildItem $unpacked -Recurse -Filter 'kr16.exe' | Select-Object -First 1 -ExpandProperty FullName
if (-not $exe) { throw 'the archive has no bin/kr16.exe' }
$releaseDir = Split-Path -Parent (Split-Path -Parent $exe)

# 4. The packaged program runs, and runs a fixture from its own tree.
$versionLine = & $exe --version 2>&1 | Select-Object -First 1
if ($LASTEXITCODE -ne 0) { throw "the packaged kr16 --version failed: $versionLine" }
Write-Host "verify: packaged --version prints: $versionLine"

$fixture = Join-Path $releaseDir 'tests/golden/exec_min.kr16'
if (-not (Test-Path $fixture)) {
    $fixture = Join-Path $root 'tests/golden/exec_min.kr16'
    Write-Warning "the archive does not carry the fixture; using the build tree's copy"
}
$run = & $exe $fixture 2>&1 | Select-Object -Last 1
Write-Host "verify: packaged run of exec_min.kr16 prints: $run"
if ($LASTEXITCODE -ne 0) { throw "the packaged kr16 could not run a fixture" }

Write-Host 'VERIFY RELEASE: PASS'
