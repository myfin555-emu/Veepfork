// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace renderer::packing {

// RGBA8 intermediate surfaces must return to the Vita's little-endian packed
// representation on readback. Keep the guest stride in pixels, not host bytes.
inline void rgba8_to_u4u4u4u4(uint8_t *dst, const uint8_t *src, uint32_t pixel_stride, uint32_t height) {
    const auto to_unorm4 = [](uint8_t value) {
        return (static_cast<uint32_t>(value) * 15 + 127) / 255;
    };
    const size_t count = static_cast<size_t>(pixel_stride) * height;
    for (size_t pixel = 0; pixel < count; ++pixel) {
        dst[2 * pixel] = static_cast<uint8_t>(to_unorm4(src[4 * pixel]) | (to_unorm4(src[4 * pixel + 1]) << 4));
        dst[2 * pixel + 1] = static_cast<uint8_t>(to_unorm4(src[4 * pixel + 2]) | (to_unorm4(src[4 * pixel + 3]) << 4));
    }
}
} // namespace renderer::packing
