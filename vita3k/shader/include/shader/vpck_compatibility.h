// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <map>

namespace shader::usse {

// Opt-in compatibility behavior adapted from nckstwrt/Vita3K-Plus.
// Track packed writes by byte, so a later VPCK preserves earlier partial writes.
// This is translation-order tracking, not general control-flow dataflow analysis.
class VpckCompatibilityState {
    std::map<uint32_t, uint8_t> written;

    static uint32_t key(uint32_t bank, uint32_t word) {
        return (bank << 24) | (word & 0xffffff);
    }

public:
    void seed(uint32_t bank, uint32_t word_count) {
        for (uint32_t word = 0; word < word_count; ++word)
            written[key(bank, word)] = 0xf;
    }

    void record(uint32_t bank, uint32_t base_word, unsigned bytes, uint8_t mask, bool preserve) {
        for (unsigned lane = 0; lane < 4; ++lane) {
            if (!(mask & (1u << lane)))
                continue;
            const unsigned byte_offset = lane * bytes;
            const auto word_key = key(bank, base_word + byte_offset / 4);
            if (preserve)
                written[word_key] |= ((bytes >= 4 ? 0xfu : (1u << bytes) - 1) << (byte_offset % 4));
            else
                written.erase(word_key);
        }
    }

    uint8_t zero_lanes(uint32_t bank, uint32_t base_word, unsigned bytes, uint8_t mask) const {
        if (bytes != 1 && bytes != 2)
            return 0;
        const unsigned pack = 4 / bytes;
        uint8_t expanded = 0;
        for (unsigned lane = 0; lane < 4; lane += pack) {
            const uint8_t slot_mask = ((1u << pack) - 1) << lane;
            if (mask & slot_mask)
                expanded |= slot_mask;
        }
        uint8_t result = 0;
        for (unsigned lane = 0; lane < 4; ++lane) {
            if (!(expanded & (1u << lane)) || (mask & (1u << lane)))
                continue;
            const unsigned byte_offset = lane * bytes;
            const uint8_t lane_bytes = ((1u << bytes) - 1) << (byte_offset % 4);
            const auto found = written.find(key(bank, base_word + byte_offset / 4));
            if (found == written.end() || !(found->second & lane_bytes))
                result |= 1u << lane;
        }
        return result;
    }
};
} // namespace shader::usse
