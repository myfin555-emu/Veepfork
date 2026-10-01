// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later

#include <features/compatibility.h>
#include <shader/vpck_compatibility.h>

#include <gtest/gtest.h>

using shader::usse::VpckCompatibilityState;

TEST(vpck_compatibility, opt_in_is_restricted_to_tested_title_and_revision) {
    EXPECT_FALSE(features::use_killzone_vpck_workaround(false, "PCSA00107", "1.00"));
    EXPECT_TRUE(features::use_killzone_vpck_workaround(true, "PCSA00107", "1.00"));
    EXPECT_FALSE(features::use_killzone_vpck_workaround(true, "PCSA00107", "1.01"));
    EXPECT_FALSE(features::use_killzone_vpck_workaround(true, "PCSA00107", ""));
    EXPECT_FALSE(features::use_killzone_vpck_workaround(true, "PCSA00107-extra", "1.00"));
    EXPECT_FALSE(features::use_killzone_vpck_workaround(true, "PCSF00403", "1.00"));
}

TEST(vpck_compatibility, integer_address_after_partial_pack_has_no_stale_high_half) {
    VpckCompatibilityState state;
    // Killzone's VPCKU16U8 r0.x followed by a 32-bit IMAD address calculation.
    state.record(0, 0, 4, 1, false); // previous float value occupying the word
    EXPECT_EQ(state.zero_lanes(0, 0, 2, 1), 2);
    EXPECT_EQ(state.zero_lanes(0, 0, 2, 3), 0); // a complete word needs no fill
    EXPECT_EQ(state.zero_lanes(0, 0, 4, 1), 0);
}

TEST(vpck_compatibility, split_byte_packs_preserve_the_first_pair) {
    VpckCompatibilityState state;
    // VPCKU8F16 pa2.xy followed by pa2.zw. Unconditional zero filling loses xy.
    const auto initial_zero = state.zero_lanes(1, 2, 1, 0b0011);
    EXPECT_EQ(initial_zero, 0b1100);
    state.record(1, 2, 1, 0b0011 | initial_zero, true);
    EXPECT_EQ(state.zero_lanes(1, 2, 1, 0b1100), 0);
    EXPECT_EQ(state.zero_lanes(0, 2, 1, 0b1100), 0b0011); // separate register bank
}

TEST(vpck_compatibility, tracks_bytes_across_registers_and_mixed_pack_sizes) {
    VpckCompatibilityState state;
    state.record(0, 10, 1, 0b1000, true); // high byte in word 10
    EXPECT_EQ(state.zero_lanes(0, 10, 2, 0b0001), 0); // preserve the high half
    EXPECT_EQ(state.zero_lanes(0, 10, 2, 0b0100), 0b1000); // word 11 is separate
    state.record(0, 10, 2, 0b1000, true); // high half in word 11
    EXPECT_EQ(state.zero_lanes(0, 11, 1, 0b0001), 0b0010);
    state.record(0, 10, 4, 0b0010, false); // float overwrites word 11 only
    EXPECT_EQ(state.zero_lanes(0, 11, 1, 0b0001), 0b1110);
    EXPECT_EQ(state.zero_lanes(0, 10, 2, 0b0001), 0);
}

TEST(vpck_compatibility, texture_results_and_fragment_inputs_are_preserved) {
    VpckCompatibilityState state;
    state.seed(1, 8);
    EXPECT_EQ(state.zero_lanes(1, 7, 1, 1), 0);
    EXPECT_EQ(state.zero_lanes(1, 8, 1, 1), 14);
    state.record(0, 12, 2, 15, true); // packed texture sample, two words
    EXPECT_EQ(state.zero_lanes(0, 12, 2, 1), 0);
    EXPECT_EQ(state.zero_lanes(0, 13, 2, 1), 0);
    state.record(0, 12, 2, 1, false); // arithmetic reuses first word
    EXPECT_EQ(state.zero_lanes(0, 12, 2, 1), 2);
    EXPECT_EQ(state.zero_lanes(0, 13, 2, 1), 0);
}
