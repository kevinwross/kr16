#!/usr/bin/env sh
set -eu

VERSION="${1:-0.1.0}"
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT"

cmake --preset linux-gcc-release
cmake --build --preset linux-gcc-release
ctest --preset linux-gcc-release

DIST="$ROOT/dist"
STAGE="$DIST/kr16-$VERSION-linux-x64"
rm -rf "$STAGE"
mkdir -p "$STAGE/bin"
cp "build/linux-gcc-release/kr16" "$STAGE/bin/kr16"
cp README.md LICENSE THIRD_PARTY_NOTICES.md "$STAGE/"
cp -R docs examples resources vscode "$STAGE/"

ARCHIVE="$DIST/kr16-$VERSION-linux-x64.tar.gz"
rm -f "$ARCHIVE"
tar -C "$DIST" -czf "$ARCHIVE" "kr16-$VERSION-linux-x64"
( cd "$DIST" && sha256sum "$(basename "$ARCHIVE")" > SHA256SUMS )
echo "wrote $ARCHIVE"
