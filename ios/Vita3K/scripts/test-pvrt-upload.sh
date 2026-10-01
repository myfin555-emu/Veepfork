#!/usr/bin/env bash
# Exercise real texture preparation and PVRTC decoding without a GPU or game.
# Requires host FFmpeg development libraries discoverable through pkg-config.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
TEST_BUILD="$ROOT/build/ios-pvrt-tests"
mkdir -p "$TEST_BUILD"
INCLUDES=()
for dir in "$ROOT"/vita3k/*/include; do
    INCLUDES+=("-I$dir")
done
read -r -a FFMPEG_FLAGS <<< "$(pkg-config --cflags --libs libswscale libavutil)"
c++ -std=c++20 -g -O1 -fsanitize=address,undefined -DFMT_HEADER_ONLY -DSPDLOG_FMT_EXTERNAL \
    "${INCLUDES[@]}" -I"$ROOT/external/boost" -I"$ROOT/external/fmt/include" \
    -I"$ROOT/external/spdlog/include" -I"$ROOT/external/xxHash" \
    -I"$ROOT/external/glslang" -I"$ROOT/external/SPIRV-Headers/include" \
    "$ROOT/vita3k/renderer/src/texture/cache.cpp" \
    "$ROOT/vita3k/renderer/src/texture/format.cpp" \
    "$ROOT/vita3k/renderer/src/texture/palette.cpp" \
    "$ROOT/vita3k/renderer/src/texture/pvrt-dec.cpp" \
    "$ROOT/vita3k/renderer/src/texture/yuv.cpp" \
    "$ROOT/vita3k/gxm/src/textures.cpp" \
    "$ROOT/vita3k/ios/tests/pvrt_upload.cpp" \
    "${FFMPEG_FLAGS[@]}" -Wl,-dead_strip -o "$TEST_BUILD/pvrt-upload-tests"
"$TEST_BUILD/pvrt-upload-tests"
