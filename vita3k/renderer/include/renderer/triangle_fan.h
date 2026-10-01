// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace renderer {

// Vulkan's default provoking vertex is the second vertex of a fan triangle,
// but the first vertex of a triangle list. Rotate each triangle to preserve
// both flat interpolation and winding. Primitive restart is disabled in our
// pipelines, so all index values (including 0xffff/0xffffffff) are vertices.
template <typename Index>
void triangle_fan_to_list(std::span<const Index> fan, std::vector<Index> &triangles) {
    static_assert(std::is_same_v<Index, uint16_t> || std::is_same_v<Index, uint32_t>);
    triangles.clear();
    if (fan.size() < 3)
        return;

    triangles.resize((fan.size() - 2) * 3);
    for (size_t i = 2; i < fan.size(); ++i) {
        const size_t base = (i - 2) * 3;
        triangles[base] = fan[i - 1];
        triangles[base + 1] = fan[i];
        triangles[base + 2] = fan[0];
    }
}

} // namespace renderer
