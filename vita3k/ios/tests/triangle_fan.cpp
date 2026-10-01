// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#include <renderer/triangle_fan.h>

#include <array>
#include <cassert>
#include <limits>
#include <random>

template <typename Index>
void check() {
    std::vector<Index> output{ 42 };
    const std::array<Index, 4> quad{ 7, 2, 9, 4 };
    renderer::triangle_fan_to_list<Index>(quad, output);
    assert((output == std::vector<Index>{ 2, 9, 7, 9, 4, 7 }));

    // Incomplete primitives must not leave stale scratch indices behind.
    for (size_t count = 0; count < 3; ++count) {
        renderer::triangle_fan_to_list<Index>(std::span(quad).first(count), output);
        assert(output.empty());
    }

    // Restart is disabled: maximum and repeated indices remain valid data.
    constexpr auto max = std::numeric_limits<Index>::max();
    const std::array<Index, 5> degenerate{ max, 3, 3, max, 0 };
    renderer::triangle_fan_to_list<Index>(degenerate, output);
    assert((output == std::vector<Index>{ 3, 3, max, 3, max, max, max, 0, max }));

    // Compare oriented triangles and flat-shading sources for many fan sizes,
    // including the production fast-path limit. This detects a winding flip
    // or choosing the fan anchor as the provoking vertex.
    std::mt19937 random(20260927);
    for (size_t count : { 3, 4, 5, 16, 257, 4096 }) {
        std::vector<Index> fan(count);
        for (auto &index : fan)
            index = static_cast<Index>(random());
        renderer::triangle_fan_to_list<Index>(fan, output);
        assert(output.size() == (count - 2) * 3);
        for (size_t triangle = 0; triangle < count - 2; ++triangle) {
            const auto *v = output.data() + triangle * 3;
            const std::array<Index, 3> reference{ fan[0], fan[triangle + 1], fan[triangle + 2] };
            assert(v[0] == reference[1]); // first provoking vertex
            bool same_orientation = false;
            for (size_t rotation = 0; rotation < 3; ++rotation)
                same_orientation |= v[0] == reference[rotation] && v[1] == reference[(rotation + 1) % 3] && v[2] == reference[(rotation + 2) % 3];
            assert(same_orientation);
        }
    }
}

int main() {
    check<uint16_t>();
    check<uint32_t>();
}
