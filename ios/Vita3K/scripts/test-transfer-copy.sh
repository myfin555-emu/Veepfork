#!/usr/bin/env bash
# Exercise CPU transfer copies with immutable-source expectations and sanitizers.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
TEST_BUILD="$ROOT/build/ios-transfer-tests"
mkdir -p "$TEST_BUILD"
INCLUDES=()
for dir in "$ROOT"/vita3k/*/include; do
    INCLUDES+=("-I$dir")
done
c++ -std=c++20 -g -O1 -fsanitize=address,undefined -DFMT_HEADER_ONLY -DSPDLOG_FMT_EXTERNAL \
    "${INCLUDES[@]}" -I"$ROOT/external/boost" -I"$ROOT/external/fmt/include" \
    -I"$ROOT/external/spdlog/include" -I"$ROOT/external/xxHash" \
    -I"$ROOT/external/glslang" -I"$ROOT/external/SPIRV-Headers/include" \
    "$ROOT/vita3k/renderer/src/texture/format.cpp" \
    "$ROOT/vita3k/renderer/src/texture/pvrt-dec.cpp" \
    "$ROOT/vita3k/ios/tests/transfer_copy.cpp" \
    -Wl,-dead_strip -o "$TEST_BUILD/transfer-copy-tests"
"$TEST_BUILD/transfer-copy-tests"
