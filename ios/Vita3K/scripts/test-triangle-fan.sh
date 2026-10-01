#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
TEST_BUILD="$ROOT/build/ios-triangle-fan-tests"
mkdir -p "$TEST_BUILD"
c++ -std=c++20 -g -O1 -fsanitize=address,undefined \
    -I"$ROOT/vita3k/renderer/include" \
    "$ROOT/vita3k/ios/tests/triangle_fan.cpp" -o "$TEST_BUILD/triangle-fan-tests"
"$TEST_BUILD/triangle-fan-tests"
