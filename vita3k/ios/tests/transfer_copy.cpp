// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later

#include <renderer/transfer_copy.h>

#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <random>
#include <vector>

namespace {
// Independent byte-address oracle. The expected result always reads from an
// immutable source, and compares the entire allocation (including row padding).
template <SceGxmTransferType type>
ptrdiff_t address(const SceGxmTransferImage &image, uint32_t x, uint32_t y, size_t pixel_bytes) {
    x += image.x;
    y += image.y;
    if constexpr (type == SCE_GXM_TRANSFER_LINEAR) {
        return ptrdiff_t(y) * image.stride + x * ptrdiff_t(pixel_bytes);
    } else if constexpr (type == SCE_GXM_TRANSFER_TILED) {
        return ptrdiff_t(y / 32) * image.stride * 32
            + ((x / 32) * 1024 + (y % 32) * 32 + x % 32) * ptrdiff_t(pixel_bytes);
    } else {
        uint32_t index = 0, bit = 0;
        for (uint32_t mask = 1; mask < std::min(image.width, image.height); mask <<= 1, ++bit) {
            if (y & mask)
                index |= 1u << (2 * bit);
            if (x & mask)
                index |= 1u << (2 * bit + 1);
        }
        index += ((x >> bit) | (y >> bit)) << (2 * bit);
        return index * ptrdiff_t(pixel_bytes);
    }
}

size_t cases = 0;

template <typename T, SceGxmTransferType src_type = SCE_GXM_TRANSFER_LINEAR,
    SceGxmTransferType dst_type = SCE_GXM_TRANSFER_LINEAR,
    SceGxmTransferColorKeyMode mode = SCE_GXM_TRANSFER_COLORKEY_NONE>
void check(ptrdiff_t src_base, ptrdiff_t dst_base, SceGxmTransferImage src, SceGxmTransferImage dst) {
    constexpr uint32_t key_mask = 0x3, key_value = 0x1;
    std::vector<T> pixels(32768);
    auto *bytes = reinterpret_cast<uint8_t *>(pixels.data());
    std::mt19937 random(12345);
    for (size_t i = 0; i < pixels.size() * sizeof(T); ++i)
        bytes[i] = random();
    auto expected = pixels;
    auto *expected_bytes = reinterpret_cast<uint8_t *>(expected.data());
    for (uint32_t y = 0; y < src.height; ++y) {
        for (uint32_t x = 0; x < src.width; ++x) {
            const ptrdiff_t source = src_base * sizeof(T) + address<src_type>(src, x, y, sizeof(T));
            const ptrdiff_t target = dst_base * sizeof(T) + address<dst_type>(dst, x, y, sizeof(T));
            assert(source >= 0 && target >= 0);
            assert(source + sizeof(T) <= pixels.size() * sizeof(T));
            assert(target + sizeof(T) <= pixels.size() * sizeof(T));
            T value;
            std::memcpy(&value, bytes + source, sizeof(T));
            if constexpr (mode == SCE_GXM_TRANSFER_COLORKEY_PASS) {
                if ((value & key_mask) != key_value)
                    continue;
            } else if constexpr (mode == SCE_GXM_TRANSFER_COLORKEY_REJECT) {
                if ((value & key_mask) == key_value)
                    continue;
            }
            std::memcpy(expected_bytes + target, &value, sizeof(T));
        }
    }
    renderer::transfer::copy<T, mode, src_type, dst_type>(pixels.data() + src_base, pixels.data() + dst_base, src, dst, key_value, key_mask);
    assert(std::memcmp(pixels.data(), expected.data(), pixels.size() * sizeof(T)) == 0);
    ++cases;
}

SceGxmTransferImage rectangle(uint32_t width, uint32_t height, int stride, uint32_t x = 0, uint32_t y = 0) {
    SceGxmTransferImage result{};
    result.width = width;
    result.height = height;
    result.stride = stride;
    result.x = x;
    result.y = y;
    return result;
}

template <typename T>
void check_pixel_size() {
    const auto full = rectangle(16, 8, 16 * sizeof(T));
    const auto padded = rectangle(16, 8, 24 * sizeof(T), 2, 1);
    const auto other_stride = rectangle(16, 8, 20 * sizeof(T), 1, 2);
    const auto negative = rectangle(16, 8, -24 * int(sizeof(T)), 2, 1);
    for (const ptrdiff_t offset : { -128, -16, -1, 0, 1, 16, 128, 4096 }) {
        check<T>(4096, 4096 + offset, full, full);
        check<T>(4096, 4096 + offset, padded, padded);
        check<T>(4096, 4096 + offset, padded, other_stride);
        check<T>(4096, 4096 + offset, negative, other_stride);
        check<T>(4096, 4096 + offset, padded, negative);
        check<T, SCE_GXM_TRANSFER_SWIZZLED, SCE_GXM_TRANSFER_LINEAR>(4096, 4096 + offset, full, padded);
        check<T, SCE_GXM_TRANSFER_LINEAR, SCE_GXM_TRANSFER_SWIZZLED>(4096, 4096 + offset, padded, full);
        check<T, SCE_GXM_TRANSFER_SWIZZLED, SCE_GXM_TRANSFER_SWIZZLED>(4096, 4096 + offset, full, full);
        const auto tiled = rectangle(40, 36, 96 * sizeof(T), 5, 9);
        const auto linear = rectangle(40, 36, 64 * sizeof(T), 3, 5);
        check<T, SCE_GXM_TRANSFER_TILED, SCE_GXM_TRANSFER_LINEAR>(4096, 4096 + offset, tiled, linear);
        check<T, SCE_GXM_TRANSFER_LINEAR, SCE_GXM_TRANSFER_TILED>(4096, 4096 + offset, linear, tiled);
        check<T, SCE_GXM_TRANSFER_TILED, SCE_GXM_TRANSFER_TILED>(4096, 4096 + offset, tiled, tiled);
        auto negative_tiled = tiled;
        negative_tiled.stride = -negative_tiled.stride;
        check<T, SCE_GXM_TRANSFER_TILED, SCE_GXM_TRANSFER_LINEAR>(8192, 8192 + offset, negative_tiled, linear);
    }
    check<T>(4096, 4097, rectangle(0, 8, 16 * sizeof(T)), full);
    check<T>(4096, 4097, rectangle(16, 0, 16 * sizeof(T)), full);
}
} // namespace

int main() {
    check_pixel_size<uint8_t>();
    check_pixel_size<uint16_t>();
    check_pixel_size<std::array<uint8_t, 3>>();
    check_pixel_size<uint32_t>();
    check_pixel_size<uint64_t>();
    check_pixel_size<std::array<uint64_t, 2>>();
    const auto keyed = rectangle(16, 8, 24 * sizeof(uint32_t), 2, 1);
    for (const ptrdiff_t offset : { -16, -1, 0, 1, 16, 4096 }) {
        check<uint32_t, SCE_GXM_TRANSFER_LINEAR, SCE_GXM_TRANSFER_LINEAR, SCE_GXM_TRANSFER_COLORKEY_PASS>(4096, 4096 + offset, keyed, keyed);
        check<uint32_t, SCE_GXM_TRANSFER_LINEAR, SCE_GXM_TRANSFER_LINEAR, SCE_GXM_TRANSFER_COLORKEY_REJECT>(4096, 4096 + offset, keyed, keyed);
    }
    std::cout << "Transfer copy passed: " << cases << " cases, overlap, layouts, strides, color keys and 8/16/24/32/64/128-bit pixels.\n";
}
