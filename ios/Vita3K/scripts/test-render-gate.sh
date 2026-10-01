#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
TEST_BUILD="$ROOT/build/ios-render-gate-tests"
mkdir -p "$TEST_BUILD"
c++ -std=c++20 -g -fsanitize=address,undefined \
    -I"$ROOT/vita3k/renderer/include" \
    "$ROOT/vita3k/ios/tests/render_gate.cpp" -o "$TEST_BUILD/render-gate-tests"
"$TEST_BUILD/render-gate-tests"
