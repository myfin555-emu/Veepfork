// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gxm/functions.h>
#include <mem/state.h>
#include <renderer/texture_cache.h>
#include <renderer/types.h>

#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

// Texture export is outside this test; fail if the upload unexpectedly calls it.
void renderer::TextureCache::export_texture_impl(SceGxmTextureBaseFormat, uint32_t, uint32_t,
    uint32_t, const void *, int, uint32_t) {
    std::abort();
}

namespace {
struct SolidColor {
    uint32_t endpoints;
    std::array<uint8_t, 4> rgba;
};

// PVRTC words with identical opaque A/B endpoints and zero modulation.
// These encode solid colors in both PVRTC1 and PVRTC2, at either bit rate.
constexpr std::array colors = {
    SolidColor{ 0xfc00fc00, { 255, 0, 0, 255 } },
    SolidColor{ 0x83e083e0, { 0, 255, 0, 255 } },
    SolidColor{ 0x801f801e, { 0, 0, 255, 255 } },
    SolidColor{ 0xfffffffe, { 255, 255, 255, 255 } },
    SolidColor{ 0x80008000, { 0, 0, 0, 255 } },
};

struct Upload {
    uint32_t width, height, mip, stride;
    int face;
    std::array<uint8_t, 4> rgba;
};

struct RecordingCache : renderer::TextureCache {
    std::vector<Upload> expected;
    size_t uploaded = 0;

    void select(size_t, const SceGxmTexture &) override { std::abort(); }
    void configure_texture(const SceGxmTexture &) override { std::abort(); }
    void import_configure_impl(SceGxmTextureBaseFormat, uint32_t, uint32_t, bool, uint16_t, uint16_t, bool) override { std::abort(); }

    void upload_texture_impl(SceGxmTextureBaseFormat format, uint32_t width, uint32_t height,
        uint32_t mip, const void *pixels, int face, uint32_t stride) override {
        assert(uploaded < expected.size());
        const auto &want = expected[uploaded++];
        // This is the representation required by the RGBA8 backend image.
        assert(format == SCE_GXM_TEXTURE_BASE_FORMAT_U8U8U8U8);
        assert(width == want.width && height == want.height && mip == want.mip);
        assert(face == want.face && stride == want.stride);
        const auto *bytes = static_cast<const uint8_t *>(pixels);
        for (uint32_t y = 0; y < height; ++y)
            for (uint32_t x = 0; x < width; ++x)
                assert(std::memcmp(bytes + 4 * (y * stride + x), want.rgba.data(), 4) == 0);
    }
};

template <typename Cache>
void advertise_legacy_native_support(Cache &cache) {
    // Also lets this regression run against the old implementation, reproducing
    // a Vulkan device that advertises PVRTC despite our RGBA8 upload contract.
    if constexpr (requires { cache.support_pvrt; })
        cache.support_pvrt = true;
}

void check_upload(SceGxmTextureBaseFormat format, renderer::Backend backend, SceGxmTextureType type) {
    RecordingCache cache;
    cache.backend = backend;
    advertise_legacy_native_support(cache);

    const bool cube = type == SCE_GXM_TEXTURE_CUBE;
    const bool arbitrary = type == SCE_GXM_TEXTURE_SWIZZLED_ARBITRARY;
    const uint32_t faces = cube ? 6 : 1;
    const uint32_t levels = arbitrary ? 1 : 3;
    const uint32_t bpp = gxm::bits_per_pixel(format);
    constexpr uint32_t address = 64;
    std::vector<uint8_t> source(address, 0);

    for (uint32_t face = 0; face < faces; ++face) {
        for (uint32_t mip = 0; mip < levels; ++mip) {
            const auto &color = colors[(face + mip) % colors.size()];
            const uint32_t stride = arbitrary ? 32 : 64 >> mip;
            const uint32_t height = arbitrary ? 20 : stride;
            const uint32_t width = arbitrary ? 24 : stride;
            cache.expected.push_back({ width, height, mip, stride, cube ? int(face + 1) : 0, color.rgba });
            const size_t start = source.size();
            const size_t size = stride * stride * bpp / 8;
            source.resize(start + size, 0);
            for (size_t word = start; word < source.size(); word += 8)
                std::memcpy(source.data() + word + 4, &color.endpoints, 4);
        }
        if (cube && face + 1 < faces)
            source.resize(address + ((source.size() - address + 2047) / 2048) * 2048, 0);
    }

    MemState mem{};
    mem.memory = Memory(new uint8_t[source.size()], [](uint8_t *p) { delete[] p; });
    std::memcpy(mem.memory.get(), source.data(), source.size());
    SceGxmTexture texture{};
    texture.type = uint32_t(type) >> 29;
    texture.base_format = (uint32_t(format) >> 24) & 0x1f;
    texture.format0 = uint32_t(format) >> 31;
    texture.data_addr = address >> 2;
    texture.mip_count = levels - 1;
    if (arbitrary) {
        texture.width = 23;
        texture.height = 19;
    } else {
        texture.width_base2 = 6;
        texture.height_base2 = 6;
    }
    cache.upload_texture(texture, mem);
    assert(cache.uploaded == cache.expected.size());
}
} // namespace

int main() {
    for (const auto format : { SCE_GXM_TEXTURE_BASE_FORMAT_PVRT2BPP, SCE_GXM_TEXTURE_BASE_FORMAT_PVRT4BPP,
             SCE_GXM_TEXTURE_BASE_FORMAT_PVRTII2BPP, SCE_GXM_TEXTURE_BASE_FORMAT_PVRTII4BPP })
        for (const auto backend : { renderer::Backend::Vulkan, renderer::Backend::OpenGL })
            for (const auto type : { SCE_GXM_TEXTURE_SWIZZLED, SCE_GXM_TEXTURE_CUBE, SCE_GXM_TEXTURE_SWIZZLED_ARBITRARY })
                check_upload(format, backend, type);
    std::cout << "PVRTC uploads passed: 4 formats, 2 backends, mip chains, cube faces and arbitrary dimensions.\n";
}
