# Builds the throwaway two-pane debugger mockup.
#
# Prerequisites:
#   - a release build of the toolchain, e.g. build\windows-ninja-release\kr16.exe
#     and kr16_core.lib (cmake --build --preset windows-ninja-release)
#   - a Developer PowerShell (cl.exe on PATH)
#
# Usage:
#   .\tools\mockup\build.ps1
#   .\tools\mockup\build.ps1 -Config windows-ninja-release
#
# Outputs (all under build\mock):
#   demo.c, demo.s, demo.o, demo.libc.o, demo.kr16
#   kr16_mockup.exe

param(
    [string]$Config = "windows-ninja-release"
)

$ErrorActionPreference = "Stop"

# cl.exe may be on PATH without the Windows SDK include/lib environment (for
# example in a plain terminal). Bootstrap vcvars64 when INCLUDE is missing.
function Import-VsEnvironment {
    if ($env:INCLUDE -and $env:LIB) {
        return
    }
    $candidates = @()
    if ($env:VSINSTALLDIR) {
        $candidates += Join-Path $env:VSINSTALLDIR "VC\Auxiliary\Build\vcvars64.bat"
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $installed = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath 2>$null
        if ($installed) {
            $candidates += Join-Path $installed.Trim() "VC\Auxiliary\Build\vcvars64.bat"
        }
    }
    $candidates += Join-Path ${env:ProgramFiles} "Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
    $vcvars = $candidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    if (-not $vcvars) {
        throw "Could not find vcvars64.bat. Run from a Developer PowerShell for Visual Studio."
    }
    $lines = & cmd.exe /c "call `"$vcvars`" >nul 2>&1 && set"
    foreach ($line in $lines) {
        $equals = $line.IndexOf('=')
        if ($equals -gt 0) {
            $name = $line.Substring(0, $equals)
            $value = $line.Substring($equals + 1)
            Set-Item -Path "env:$name" -Value $value
        }
    }
}

Import-VsEnvironment

$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $root "build"
$core = Join-Path $build "$Config\kr16_core.lib"
$kr16 = Join-Path $build "$Config\kr16.exe"
$out = Join-Path $build "mock"

if (-not (Test-Path $core)) {
    throw "Missing $core. Build the toolchain first: cmake --build --preset $Config"
}
if (-not (Test-Path $kr16)) {
    throw "Missing $kr16."
}

$cl = Get-Command cl.exe -ErrorAction SilentlyContinue
if ($null -eq $cl) {
    throw "cl.exe not on PATH. Run this from a Developer PowerShell for Visual Studio."
}

New-Item -ItemType Directory -Force -Path $out | Out-Null

# Embed resources/libc.s so loading a .c file works without a resources folder
# beside the executable.
$libcSource = Join-Path $root "resources\libc.s"
if (-not (Test-Path $libcSource)) {
    throw "Missing $libcSource."
}
$libc = Get-Content $libcSource -Raw
$embedded = @(
    'extern const char kEmbeddedLibc[];'
    'const char kEmbeddedLibc[] = R"KR16LIBC('
)
$embedded += $libc
$embedded += ')KR16LIBC";'
Set-Content -Path (Join-Path $out "embedded_libc.cpp") -Value $embedded -Encoding ascii

# Build the sample program next to the mock so its source path stays absolute
# and the generated .s/.o files stay out of the source tree.
Copy-Item (Join-Path $PSScriptRoot "demo.c") (Join-Path $out "demo.c") -Force
& $kr16 build (Join-Path $out "demo.c") -o (Join-Path $out "demo.kr16")
if ($LASTEXITCODE -ne 0) {
    throw "kr16 build failed for demo.c"
}

Push-Location $root
try {
    & $cl.Source /nologo /std:c++20 /EHsc /MT /O2 /DNDEBUG `
        "/I$root\include" `
        "/Fo$out\" `
        "/Fd$out\kr16_mockup.pdb" `
        "/Fe$out\kr16_mockup.exe" `
        (Join-Path $out "embedded_libc.cpp") `
        (Join-Path $PSScriptRoot "kr16_mockup.cpp") `
        $core `
        ws2_32.lib user32.lib gdi32.lib shell32.lib comdlg32.lib `
        /link /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup
    if ($LASTEXITCODE -ne 0) {
        throw "mockup compile/link failed"
    }
} finally {
    Pop-Location
}

Write-Host ""
Write-Host "Built $out\kr16_mockup.exe"
Write-Host "Run:   $out\kr16_mockup.exe $out\demo.kr16"
Write-Host "Or double-click kr16_mockup.exe (it loads demo.kr16 beside itself)."
