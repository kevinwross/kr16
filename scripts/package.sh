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
cp README.md LICENSE THIRD_PARTY_NOTICES.md RELEASE_NOTES-0.1.0.md "$STAGE/"
cp -R docs examples resources vscode "$STAGE/"
VSIX="$ROOT/vscode/kr16-debug-$VERSION.vsix"
if [ -f "$VSIX" ]; then
  cp "$VSIX" "$STAGE/"
fi

ARCHIVE="$DIST/kr16-$VERSION-linux-x64.tar.gz"
rm -f "$ARCHIVE"
EPOCH="${SOURCE_DATE_EPOCH:-315532800}"
tar --sort=name --mtime="@$EPOCH" --owner=0 --group=0 --numeric-owner \
  -C "$DIST" -cf - "kr16-$VERSION-linux-x64" | gzip -n > "$ARCHIVE"
( cd "$DIST" && sha256sum "$(basename "$ARCHIVE")" > SHA256SUMS )
if [ -f "$VSIX" ]; then
  ( cd "$DIST" && sha256sum "$(basename "$VSIX")" >> SHA256SUMS )
fi
echo "wrote $ARCHIVE"
