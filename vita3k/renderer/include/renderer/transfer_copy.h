// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <renderer/gxm_types.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace renderer::texture {
uint32_t encode_morton(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
}

namespace renderer::transfer {

template <typename T, SceGxmTransferType type>
ptrdiff_t pixel_offset(uint32_t x, uint32_t y, const SceGxmTransferImage &image) {
    const ptrdiff_t stride = image.stride / static_cast<ptrdiff_t>(sizeof(T));
    if constexpr (type == SCE_GXM_TRANSFER_LINEAR) {
        return y * stride + x;
    } else if constexpr (type == SCE_GXM_TRANSFER_TILED) {
        return ((stride / 32) * (y / 32) + x / 32) * 1024 + (y % 32) * 32 + x % 32;
    } else {
        return texture::encode_morton(x, y, image.width, image.height);
    }
}

struct AddressRange {
    intptr_t begin;
    intptr_t end;
};

template <typename T, SceGxmTransferType type>
AddressRange address_range(const T *pixels, const SceGxmTransferImage &image, uint32_t width, uint32_t height) {
    ptrdiff_t first = pixel_offset<T, type>(image.x, image.y, image);
    ptrdiff_t last = first;
    // X offsets are monotonic within each row. Checking every row also covers
    // tiled images with negative strides, whose endpoints alone are insufficient.
    for (uint32_t y = 0; y < height; ++y) {
        first = std::min(first, pixel_offset<T, type>(image.x, image.y + y, image));
        last = std::max(last, pixel_offset<T, type>(image.x + width - 1, image.y + y, image));
    }
    const intptr_t base = reinterpret_cast<intptr_t>(pixels);
    return { base + first * static_cast<ptrdiff_t>(sizeof(T)), base + (last + 1) * static_cast<ptrdiff_t>(sizeof(T)) };
}

// Transfers may relocate pixels within one allocation. Preserve the source
// rectangle before any destination writes, including conversions between layouts.
// Return whether the address ranges overlap, for bounded diagnostic sampling.
template <typename T, SceGxmTransferColorKeyMode mode, SceGxmTransferType src_type, SceGxmTransferType dst_type>
bool copy(const T *src_pixels, T *dst_pixels, const SceGxmTransferImage &src, const SceGxmTransferImage &dst,
    uint32_t key_value, uint32_t key_mask) {
    if (src.width == 0 || src.height == 0)
        return false;

    const auto src_range = address_range<T, src_type>(src_pixels, src, src.width, src.height);
    const auto dst_range = address_range<T, dst_type>(dst_pixels, dst, src.width, src.height);
    const bool overlap = src_range.begin < dst_range.end && dst_range.begin < src_range.end;

    if constexpr (src_type == SCE_GXM_TRANSFER_LINEAR && dst_type == SCE_GXM_TRANSFER_LINEAR
        && mode == SCE_GXM_TRANSFER_COLORKEY_NONE) {
        const size_t row_bytes = static_cast<size_t>(src.width) * sizeof(T);
        if (src.stride > 0 && dst.stride > 0
            && static_cast<size_t>(src.stride) == row_bytes && static_cast<size_t>(dst.stride) == row_bytes) {
            std::memmove(dst_pixels + pixel_offset<T, dst_type>(dst.x, dst.y, dst),
                src_pixels + pixel_offset<T, src_type>(src.x, src.y, src), row_bytes * src.height);
            return overlap;
        }
    }

    std::vector<T> snapshot;
    if (overlap) {
        snapshot.resize(static_cast<size_t>(src.width) * src.height);
        for (uint32_t y = 0; y < src.height; ++y)
            for (uint32_t x = 0; x < src.width; ++x)
                snapshot[static_cast<size_t>(y) * src.width + x] = src_pixels[pixel_offset<T, src_type>(src.x + x, src.y + y, src)];
    }

    for (uint32_t y = 0; y < src.height; ++y) {
        for (uint32_t x = 0; x < src.width; ++x) {
            const T value = overlap ? snapshot[static_cast<size_t>(y) * src.width + x]
                                    : src_pixels[pixel_offset<T, src_type>(src.x + x, src.y + y, src)];
            if constexpr (mode == SCE_GXM_TRANSFER_COLORKEY_PASS) {
                if ((value & key_mask) != key_value)
                    continue;
            } else if constexpr (mode == SCE_GXM_TRANSFER_COLORKEY_REJECT) {
                if ((value & key_mask) == key_value)
                    continue;
            }
            dst_pixels[pixel_offset<T, dst_type>(dst.x + x, dst.y + y, dst)] = value;
        }
    }
    return overlap;
}

} // namespace renderer::transfer
