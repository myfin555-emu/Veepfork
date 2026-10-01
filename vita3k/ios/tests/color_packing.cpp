// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later
#include <renderer/color_packing.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    constexpr uint32_t count = 65536;
    std::vector<uint8_t> rgba(count * 4);
    std::vector<uint8_t> packed(count * 2 + 2, 0xa5);
    for (uint32_t value = 0; value < count; ++value) {
        for (unsigned component = 0; component < 4; ++component)
            rgba[value * 4 + component] = ((value >> (component * 4)) & 15) * 17;
    }
    // Every original 16-bit color must survive expansion and readback, across
    // multiple rows and with an unaligned guest address.
    renderer::packing::rgba8_to_u4u4u4u4(packed.data() + 1, rgba.data(), 256, 256);
    for (uint32_t value = 0; value < count; ++value)
        assert((uint32_t(packed[1 + 2 * value]) | (uint32_t(packed[2 + 2 * value]) << 8)) == value);
    assert(packed.front() == 0xa5 && packed.back() == 0xa5);

    // Values adjacent to a quantization boundary, endpoints and alpha.
    const std::array<uint8_t, 8> boundary{ 8, 9, 246, 247, 0, 255, 0, 255 };
    std::array<uint8_t, 4> result{};
    renderer::packing::rgba8_to_u4u4u4u4(result.data(), boundary.data(), 1, 2);
    assert((result == std::array<uint8_t, 4>{ 0x10, 0xfe, 0xf0, 0xf0 }));
    std::cout << "65536 color roundtrips, stride, alignment and rounding boundaries passed\n";
}
