#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build/linux-package}"
DIST="${DIST_DIR:-$ROOT/dist}"
APP="$BUILD/KR16.AppDir"
ARCH_NAME="$(uname -m)"
case "$ARCH_NAME" in
  x86_64) APPIMAGE_ARCH=x86_64 ;;
  aarch64|arm64) APPIMAGE_ARCH=aarch64 ;;
  *) echo "unsupported architecture: $ARCH_NAME" >&2; exit 2 ;;
esac
APPIMAGE_ARCH="${KR16_APPIMAGE_ARCH:-$APPIMAGE_ARCH}"
BINARY="${KR16_BINARY:-$BUILD/kr16}"

if [ -z "${KR16_BINARY:-}" ]; then
  cmake -S "$ROOT" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DKR16_BUILD_UI=ON \
    -DKR16_BUILD_TESTS=OFF \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"
  cmake --build "$BUILD" --target kr16 -j "$(nproc)"
  BINARY="$BUILD/kr16"
fi

rm -rf "$APP"
install -Dm755 "$BINARY" "$APP/usr/bin/kr16"
install -Dm644 "$ROOT/resources/libc.s" \
  "$APP/usr/share/kr16/resources/libc.s"
install -Dm644 "$ROOT/packaging/linux/kr16.desktop" \
  "$APP/usr/share/applications/kr16.desktop"
install -Dm644 "$ROOT/packaging/linux/kr16.desktop" "$APP/kr16.desktop"
install -Dm644 "$ROOT/packaging/linux/kr16.png" \
  "$APP/usr/share/icons/hicolor/256x256/apps/kr16.png"
install -Dm644 "$ROOT/packaging/linux/kr16.png" "$APP/kr16.png"
install -Dm755 "$ROOT/packaging/linux/AppRun" "$APP/AppRun"

strip --strip-unneeded "$APP/usr/bin/kr16" || true

TOOL="${APPIMAGETOOL:-$BUILD/appimagetool-$APPIMAGE_ARCH.AppImage}"
if [ ! -x "$TOOL" ]; then
  curl -fL --retry 3 \
    "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-$APPIMAGE_ARCH.AppImage" \
    -o "$TOOL"
  chmod +x "$TOOL"
fi

mkdir -p "$DIST"
OUT="$DIST/KR16-$APPIMAGE_ARCH.AppImage"
ARCH="$APPIMAGE_ARCH" "$TOOL" --appimage-extract-and-run --no-appstream \
  "$APP" "$OUT"
chmod +x "$OUT"
echo "wrote $OUT"
