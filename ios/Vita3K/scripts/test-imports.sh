#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
TEST_BUILD="$ROOT/build/ios-import-tests"
mkdir -p "$TEST_BUILD"
cc -g -O1 -fsanitize=address,undefined -I"$ROOT/external/miniz" \
    -c "$ROOT/external/miniz/miniz.c" -o "$TEST_BUILD/miniz.o"
c++ -std=c++20 -g -O1 -fsanitize=address,undefined -DFMT_HEADER_ONLY \
    -I"$ROOT/vita3k/ios/include" -I"$ROOT/external/miniz" \
    -I"$ROOT/vita3k/packages/include" -I"$ROOT/vita3k/io/include" \
    -I"$ROOT/vita3k/util/include" -I"$ROOT/external/boost" -I"$ROOT/external/fmt/include" \
    "$ROOT/vita3k/ios/src/import_archive.cpp" "$ROOT/vita3k/packages/src/sfo.cpp" \
    "$ROOT/vita3k/ios/tests/import_archive.cpp" "$TEST_BUILD/miniz.o" -o "$TEST_BUILD/import-tests"
python3 "$ROOT/ios/Vita3K/scripts/tests/test_import_archives.py" "$TEST_BUILD/import-tests"
