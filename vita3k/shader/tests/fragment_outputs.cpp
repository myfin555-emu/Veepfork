// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later

#include <features/compatibility.h>
#include <shader/spirv_recompiler.h>
#include <gtest/gtest.h>
#include <spirv_cross.hpp>
#include <cstddef>
#include <map>

namespace {
// Synthetic program exercises the real finalizer without shipping game shaders.
struct FragmentFixture {
    SceGxmProgram program{};
    SceGxmProgramVertexVaryings varyings{};
    uint64_t instructions = 0;

    FragmentFixture(SceGxmParameterType type, unsigned offset, bool native) {
        program.magic = 0x00505847;
        program.major_version = 1;
        program.minor_version = 4;
        program.size = sizeof(*this);
        program.program_flags = SCE_GXM_PROGRAM_FLAG_FRAGMENT | (native ? SCE_GXM_PROGRAM_FLAG_NATIVECOLOR_USED : 0);
        program.primary_reg_count = 8;
        program.varyings_offset = offsetof(FragmentFixture, varyings) - offsetof(SceGxmProgram, varyings_offset);
        program.primary_program_offset = offsetof(FragmentFixture, instructions) - offsetof(SceGxmProgram, primary_program_offset);
        program.secondary_program_offset = offsetof(FragmentFixture, instructions) - offsetof(SceGxmProgram, secondary_program_offset);
        program.secondary_program_offset_end = offsetof(FragmentFixture, instructions) - offsetof(SceGxmProgram, secondary_program_offset_end);
        varyings.output_param_type = type;
        varyings.output_comp_count = 2;
        varyings.fragment_output_start = offset;
    }

    shader::usse::SpirvCode compile(bool raw) const {
        FeatureState features{};
        features.preserve_f16_nan_as_u16 = raw;
        shader::Hints hints{};
        hints.color_format = SCE_GXM_COLOR_FORMAT_F16F16F16F16_ABGR;
        return shader::convert_gxp(program, "fragment-output-test", features, shader::Target::SpirVVulkan, hints).spirv;
    }
};

bool has_word_pair(const shader::usse::SpirvCode &code, unsigned first) {
    spirv_cross::Compiler reflection(code);
    std::map<unsigned, std::pair<unsigned, unsigned>> extracts;
    for (size_t offset = 5; offset < code.size(); offset += code[offset] >> 16) {
        const auto length = code[offset] >> 16;
        const auto op = code[offset] & 0xffff;
        if (op == spv::OpVectorExtractDynamic)
            extracts[code[offset + 2]] = { code[offset + 3], reflection.get_constant(code[offset + 4]).scalar() };
        if (op == spv::OpCompositeConstruct && length == 5) {
            const auto a = extracts.find(code[offset + 3]), b = extracts.find(code[offset + 4]);
            if (a != extracts.end() && b != extracts.end()
                && a->second.first == b->second.first && a->second.second == first && b->second.second == first + 1)
                return true;
        }
    }
    return false;
}
} // namespace

TEST(fragment_outputs, packed_words_follow_primary_attribute_offset) {
    for (const auto type : { SCE_GXM_PARAMETER_TYPE_F16, SCE_GXM_PARAMETER_TYPE_U32 }) {
        const auto zero = FragmentFixture(type, 0, false).compile(true);
        const auto shifted = FragmentFixture(type, 2, false).compile(true);
        EXPECT_TRUE(has_word_pair(zero, 0));
        EXPECT_FALSE(has_word_pair(zero, 2));
        EXPECT_TRUE(has_word_pair(shifted, 2));
        EXPECT_FALSE(has_word_pair(shifted, 0));
        // Native-color programs use output registers, not the PA offset field.
        EXPECT_EQ(FragmentFixture(type, 0, true).compile(true), FragmentFixture(type, 2, true).compile(true));
    }
}

TEST(fragment_outputs, raw_attachment_is_unsigned_and_opt_in) {
    for (const bool raw : { false, true }) {
        spirv_cross::Compiler reflection(FragmentFixture(SCE_GXM_PARAMETER_TYPE_U32, 2, false).compile(raw));
        const auto outputs = reflection.get_shader_resources().stage_outputs;
        ASSERT_EQ(outputs.size(), raw ? 2 : 1);
        for (const auto &output : outputs) {
            const auto location = reflection.get_decoration(output.id, spv::DecorationLocation);
            const auto &type = reflection.get_type(output.type_id);
            EXPECT_EQ(type.vecsize, 4);
            EXPECT_EQ(type.basetype, location == 1 ? spirv_cross::SPIRType::UInt : spirv_cross::SPIRType::Float);
        }
    }
}

TEST(fragment_outputs, resistance_workaround_requires_exact_opt_in_title_revision) {
    EXPECT_TRUE(features::use_resistance_f16_workaround(true, "PCSA00063", "1.00"));
    EXPECT_FALSE(features::use_resistance_f16_workaround(false, "PCSA00063", "1.00"));
    EXPECT_FALSE(features::use_resistance_f16_workaround(true, "PCSA00063", "1.01"));
    EXPECT_FALSE(features::use_resistance_f16_workaround(true, "PCSA00063", ""));
    EXPECT_FALSE(features::use_resistance_f16_workaround(true, "PCSA00063-extra", "1.00"));
    EXPECT_FALSE(features::use_resistance_f16_workaround(true, "PCSF00183", "1.00"));
}
