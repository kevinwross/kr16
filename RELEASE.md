# Release Checklist

This is the operational checklist for a KR16 release. The code paths are
implemented; the steps marked **secret** need repository or local credentials
that are not stored in the repository.

## 1. Verify the tree

```text
cmake --preset windows-ninja-release
cmake --build --preset windows-ninja-release
ctest --preset windows-ninja-release

cmake --preset linux-gcc
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug
```

Both suites must pass. The Windows suite includes the installer cycle, the
embedded-resource consistency test, the resource-manifest hash check, and the
linker tests.

## 2. Check resources

```text
pwsh -NoProfile -File scripts/bootstrap-check.ps1
```

The embedded runtime is checked in. `tests/unit/test_resources.cpp` fails if
`src/resources/embedded_libc.cpp` differs from `resources/libc.s`,
`resources/fileio.c`, or `include/kr16/systemservices.h`.

## 3. Package Windows

```text
pwsh -NoProfile -File scripts/package.ps1
```

This writes `dist/kr16-1.0.0-windows-x64.zip` with `install.ps1` and
`uninstall.ps1`. With the same `SOURCE_DATE_EPOCH`, the zip is byte-identical
across runs.

Verify the installer from the extracted zip:

```text
powershell -ExecutionPolicy Bypass -File install.ps1 -InstallDir C:\temp\kr16 -NoPath -NoShortcut
C:\temp\kr16\bin\kr16.exe version
powershell -ExecutionPolicy Bypass -File C:\temp\kr16\uninstall.ps1 -InstallDir C:\temp\kr16
```

## 4. Package Linux

```text
bash packaging/linux/package-appimage.sh
```

The AppImage installs `libc.s`, `fileio.c`, and `systemservices.h` under
`usr/share/kr16`, so `kr16 build` works without an external resource tree.
`SOURCE_DATE_EPOCH` defaults to the latest commit time.

The ARM64 AppImage is built on the native ARM64 runner in
`.github/workflows/release.yml`.

## 5. Code signing — **secret**

Required repository secret:

```text
WINDOWS_PFX_BASE64
WINDOWS_PFX_PASSWORD   (optional for an unprotected PFX)
```

The Windows job decodes the PFX and sets `KR16_SIGN_CERT` /
`KR16_SIGN_PASSWORD`; `scripts/package.ps1` calls `signtool` before building
the zip. If the secret is absent the build is unsigned but still succeeds.

For a local signed package:

```text
$env:KR16_SIGN_CERT = "C:\keys\kr16.pfx"
$env:KR16_SIGN_PASSWORD = "..."
pwsh -NoProfile -File scripts/package.ps1
```

## 6. Marketplace publishing — **secret**

Required repository secret:

```text
VSCE_PAT
```

The VS Code job packages `dist/kr16-debug.vsix` unconditionally and runs
`vsce publish` only when `VSCE_PAT` is present. The extension publisher
(`kr16`) must exist on the marketplace and the token must be allowed to publish
it.

Local package check:

```text
cd vscode
npx --yes @vscode/vsce package --no-dependencies -o ../dist/kr16-debug.vsix
```

## 7. Publish the GitHub release

The tag workflow runs the Windows, Linux x64, Linux ARM64, and VS Code jobs,
then uploads:

- `kr16.exe`
- `kr16-1.0.0-windows-x64.zip`
- `KR16-x86_64.AppImage`
- `KR16-aarch64.AppImage`
- `kr16-debug.vsix`
- `SHA256SUMS`

Before tagging, confirm the intended versions in `CMakeLists.txt`,
`vscode/package.json`, and `RELEASE_NOTES-1.0.0.md`, and that the workflow's
artifact filenames match the package scripts.

## What still needs a human

- provisioning the signing certificate
- provisioning `VSCE_PAT` and confirming the marketplace publisher
- running the tag workflow and checking the published release assets

