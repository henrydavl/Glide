#!/usr/bin/env bash
# Dev build on macOS. Needs the ARM Homebrew deps: brew install qtbase mpv pkgconf cmake
set -euo pipefail
cd "$(dirname "$0")/.."

export PATH="/opt/homebrew/bin:$PATH"
export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig"

cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build -j "$(sysctl -n hw.ncpu)"
echo "Built: build/Glide.app  (run: open build/Glide.app --args <file.ts>)"
