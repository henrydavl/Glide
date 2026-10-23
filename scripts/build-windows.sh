#!/usr/bin/env bash
# Builds a self-contained, portable Windows folder in dist/Glide.
# Run inside an MSYS2 "UCRT64" shell after installing:
#   pacman -S --needed mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,pkgconf,qt6-base,mpv}
set -euo pipefail
cd "$(dirname "$0")/.."

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

rm -rf dist/Glide
mkdir -p dist/Glide
cp build/Glide.exe dist/Glide/

windeployqt=$(command -v windeployqt6 || command -v windeployqt-qt6 || command -v windeployqt || true)
if [ -z "$windeployqt" ]; then
    echo "windeployqt not found (it ships with qt6-base)" >&2
    exit 1
fi
"$windeployqt" --release --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime dist/Glide/Glide.exe

# Copy every MSYS2 DLL the exe and the Qt plugins load: libmpv, FFmpeg, libplacebo, the
# compiler runtime, and so on. ldd already lists transitive dependencies.
find dist/Glide \( -name '*.exe' -o -name '*.dll' \) -print0 \
    | xargs -0 ldd 2>/dev/null \
    | awk '/\/ucrt64\// { print $3 }' \
    | sort -u \
    | while read -r dll; do
        [ -e "dist/Glide/$(basename "$dll")" ] || cp "$dll" dist/Glide/
    done

echo "Built dist/Glide ($(du -sh dist/Glide | cut -f1)). Run dist/Glide/Glide.exe"
