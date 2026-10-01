#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BUILD_DIR="$ROOT/build/color-packing-tests"
mkdir -p "$BUILD_DIR"
c++ -std=c++20 -g -O1 -fsanitize=address,undefined \
    -I"$ROOT/vita3k/renderer/include" "$ROOT/vita3k/ios/tests/color_packing.cpp" \
    -o "$BUILD_DIR/color-packing-tests"
"$BUILD_DIR/color-packing-tests"
