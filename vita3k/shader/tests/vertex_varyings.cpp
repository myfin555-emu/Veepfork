// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later

#include <shader/spirv_recompiler.h>

#include <gtest/gtest.h>
#include <spirv_cross.hpp>

#include <cstddef>
#include <set>

namespace {
// Synthetic GXP with no instructions: exercise the real vertex finalizer without
// distributing a game's shader. Use the sparse interface seen in DOAX3.
struct VertexFixture {
    SceGxmProgram program{};
    SceGxmProgramVertexVaryings varyings{};
    uint64_t instructions = 0;

    VertexFixture() {
        program.magic = 0x00505847;
        program.major_version = 1;
        program.minor_version = 4;
        program.size = sizeof(*this);
        program.varyings_offset = offsetof(VertexFixture, varyings) - offsetof(SceGxmProgram, varyings_offset);
        program.primary_program_offset = offsetof(VertexFixture, instructions) - offsetof(SceGxmProgram, primary_program_offset);
        program.secondary_program_offset = offsetof(VertexFixture, instructions) - offsetof(SceGxmProgram, secondary_program_offset);
        program.secondary_program_offset_end = offsetof(VertexFixture, instructions) - offsetof(SceGxmProgram, secondary_program_offset_end);
        varyings.vertex_outputs1 = 0x400; // COLOR1
        for (unsigned coord : { 0, 3, 8, 9 })
            varyings.vertex_outputs2 |= 7u << (3 * coord); // xyzw
    }

    shader::usse::SpirvCode compile(shader::Target target) const {
        shader::Hints hints{};
        hints.color_format = SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR;
        return shader::convert_gxp(program, "vertex-varyings-test", FeatureState{}, target, hints).spirv;
    }
};

const std::set<unsigned> declared_locations{ 2, 4, 7, 12, 13 };

void check_outputs(const shader::usse::SpirvCode &code, const std::set<unsigned> &expected) {
    spirv_cross::Compiler reflection(code);
    std::set<unsigned> actual;
    for (const auto &output : reflection.get_shader_resources().stage_outputs) {
        actual.insert(reflection.get_decoration(output.id, spv::DecorationLocation));
        const auto &type = reflection.get_type(output.type_id);
        EXPECT_EQ(type.basetype, spirv_cross::SPIRType::Float);
        EXPECT_EQ(type.vecsize, 4);
    }
    EXPECT_EQ(actual, expected);
}
} // namespace

TEST(vertex_varyings, sparse_interface) {
    VertexFixture fixture;
    check_outputs(fixture.compile(shader::Target::SpirVOpenGL), declared_locations);
    const auto code = fixture.compile(shader::Target::SpirVVulkan);
#ifdef __APPLE__
    check_outputs(code, { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 });
    spirv_cross::Compiler reflection(code);
    for (const auto &output : reflection.get_shader_resources().stage_outputs) {
        const unsigned location = reflection.get_decoration(output.id, spv::DecorationLocation);
        if (declared_locations.contains(location))
            continue;
        unsigned stores = 0;
        for (size_t offset = 5; offset < code.size(); offset += code[offset] >> 16) {
            if ((code[offset] & 0xffff) == spv::OpStore && code[offset + 1] == output.id) {
                const auto &value = reflection.get_constant(code[offset + 2]);
                for (unsigned component = 0; component < 4; ++component)
                    EXPECT_EQ(value.scalar_f32(0, component), 0.0f);
                ++stores;
            }
        }
        EXPECT_EQ(stores, 1); // a declaration alone does not satisfy Metal
    }
#else
    check_outputs(code, declared_locations);
#endif
}

TEST(vertex_varyings, complete_interface_has_no_duplicate_outputs) {
    VertexFixture fixture;
    fixture.varyings.vertex_outputs1 = 0xe00; // colors + fog
    fixture.varyings.vertex_outputs2 = 0x3fffffff; // all ten texcoords
    check_outputs(fixture.compile(shader::Target::SpirVVulkan), { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 });
}

TEST(vertex_varyings, point_size_remains_a_scalar_builtin) {
    VertexFixture fixture;
    fixture.varyings.vertex_outputs1 |= 0x100;
    spirv_cross::Compiler reflection(fixture.compile(shader::Target::SpirVVulkan));
    unsigned found = 0;
    for (const auto &output : reflection.get_shader_resources().builtin_outputs) {
        if (output.builtin == spv::BuiltInPointSize) {
            EXPECT_EQ(reflection.get_type(output.value_type_id).vecsize, 1);
            ++found;
        }
    }
    EXPECT_EQ(found, 1);
}
