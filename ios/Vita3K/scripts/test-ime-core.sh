#!/usr/bin/env bash
# Exercise the native keyboard's real core helpers without firmware or JIT.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
TEST_BUILD="$ROOT/build/ios-ime-tests"
mkdir -p "$TEST_BUILD"
INCLUDES=()
for dir in "$ROOT"/vita3k/*/include; do
    INCLUDES+=("-I$dir")
done
c++ -std=c++20 -g -fsanitize=address,undefined "${INCLUDES[@]}" \
    -I"$ROOT/external/boost" -I"$ROOT/external/fmt/include" -I"$ROOT/external/spdlog/include" \
    "$ROOT/vita3k/ios/src/ime.cpp" "$ROOT/vita3k/ios/tests/ime.cpp" \
    -o "$TEST_BUILD/ime-core-tests"
"$TEST_BUILD/ime-core-tests"
