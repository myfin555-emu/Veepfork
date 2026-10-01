// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later

#include <shader/gxp_parser.h>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>

namespace {
struct UniformFixture {
    SceGxmProgram program{};
    SceGxmProgramVertexVaryings varyings{};
    std::array<SceGxmProgramParameter, 2> parameters{};
    SceGxmProgramParameterContainer container{};

    UniformFixture(bool member_first, bool include_member) {
        program.magic = 0x00505847;
        program.size = sizeof(*this);
        program.primary_reg_count = 4;
        program.varyings_offset = offsetof(UniformFixture, varyings) - offsetof(SceGxmProgram, varyings_offset);
        program.parameters_offset = offsetof(UniformFixture, parameters) - offsetof(SceGxmProgram, parameters_offset);
        program.parameter_count = include_member ? 2 : 1;
        program.container_offset = offsetof(UniformFixture, container) - offsetof(SceGxmProgram, container_offset);
        program.container_count = 1;
        container.container_index = 4;
        container.base_sa_offset = 8;
        container.size_in_f32 = 4;

        SceGxmProgramParameter block{};
        block.category = SCE_GXM_PARAMETER_CATEGORY_UNIFORM_BUFFER;
        block.resource_index = 4;
        block.array_size = 64;
        SceGxmProgramParameter member{};
        member.category = SCE_GXM_PARAMETER_CATEGORY_UNIFORM;
        member.type = SCE_GXM_PARAMETER_TYPE_F32;
        member.component_count = 4;
        member.container_index = 4;
        member.array_size = 1;
        parameters[0] = include_member && member_first ? member : block;
        parameters[1] = member_first ? block : member;
    }
};
} // namespace

TEST(uniform_containers, register_window_is_loaded_without_member_symbols) {
    const UniformFixture fixture(false, false);
    const auto inputs = shader::get_program_input(fixture.program);
    ASSERT_EQ(inputs.uniform_buffers.size(), 1);
    EXPECT_EQ(inputs.uniform_buffers[0].index, 4);
    EXPECT_EQ(inputs.uniform_buffers[0].reg_start_offset, 8);
    EXPECT_EQ(inputs.uniform_buffers[0].reg_block_size, 4);
}

TEST(uniform_containers, register_window_is_independent_of_parameter_order) {
    for (const bool member_first : { false, true }) {
        const UniformFixture fixture(member_first, true);
        const auto inputs = shader::get_program_input(fixture.program);
        ASSERT_EQ(inputs.uniform_buffers.size(), 1);
        EXPECT_EQ(inputs.uniform_buffers[0].reg_start_offset, 8);
        EXPECT_EQ(inputs.uniform_buffers[0].reg_block_size, 4);
        EXPECT_EQ(inputs.uniform_buffers[0].size, 16);
    }
}

TEST(uniform_containers, memory_only_buffer_keeps_no_register_window) {
    UniformFixture fixture(false, false);
    fixture.program.container_count = 0;
    const auto inputs = shader::get_program_input(fixture.program);
    ASSERT_EQ(inputs.uniform_buffers.size(), 1);
    EXPECT_EQ(inputs.uniform_buffers[0].reg_start_offset, 0);
    EXPECT_EQ(inputs.uniform_buffers[0].reg_block_size, 0);
}
