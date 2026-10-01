// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later

#include <shader/spirv_recompiler.h>
#include <shader/usse_program_analyzer.h>

#include <GLSL.std.450.h>
#include <gtest/gtest.h>
#include <spirv_cross.hpp>

#include <array>
#include <cstddef>
#include <map>
#include <string_view>

namespace {
struct ComplexFixture {
    SceGxmProgram program{};
    SceGxmProgramVertexVaryings varyings{};
    std::array<uint64_t, 2> instructions{};

    explicit ComplexFixture(uint64_t instruction) {
        program.magic = 0x00505847;
        program.major_version = 1;
        program.minor_version = 4;
        program.size = sizeof(*this);
        program.primary_reg_count = 32;
        program.temp_reg_count1 = 32;
        program.varyings_offset = offsetof(ComplexFixture, varyings) - offsetof(SceGxmProgram, varyings_offset);
        program.primary_program_offset = offsetof(ComplexFixture, instructions) - offsetof(SceGxmProgram, primary_program_offset);
        program.primary_program_instr_count = instructions.size();
        program.secondary_program_offset = offsetof(ComplexFixture, instructions) - offsetof(SceGxmProgram, secondary_program_offset);
        program.secondary_program_offset_end = offsetof(ComplexFixture, instructions) - offsetof(SceGxmProgram, secondary_program_offset_end);
        instructions = { 0xfa44070000000000, instruction }; // PHAS, complex op
    }

    shader::usse::SpirvCode compile(shader::Target target, const FeatureState &features = {}) const {
        shader::Hints hints{};
        hints.color_format = SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR;
        return shader::convert_gxp(program, "complex-instructions-test", features, target, hints).spirv;
    }
};

void check_complex(uint64_t instruction, unsigned expected_builtin, bool exponential) {
    for (const auto target : { shader::Target::SpirVOpenGL, shader::Target::SpirVVulkan }) {
        const auto code = ComplexFixture(instruction).compile(target);
        std::map<unsigned, unsigned> builtins;
        unsigned nan_checks = 0;
        unsigned nan_selects = 0;
        for (size_t offset = 5; offset < code.size(); offset += code[offset] >> 16) {
            const auto op = code[offset] & 0xffff;
            if (op == spv::OpExtInst)
                ++builtins[code[offset + 4]];
            nan_checks += op == spv::OpIsNan;
            nan_selects += op == spv::OpSelect;
        }
        // Check the real instruction decoder and both lowering paths, including
        // F16 operands. Natural-base lowering is wrong for standalone USSE ops;
        // testing only a LOG/MUL/EXP power sequence would hide this error.
        EXPECT_EQ(builtins[expected_builtin], 1);
        EXPECT_EQ(builtins[GLSLstd450Exp], 0);
        EXPECT_EQ(builtins[GLSLstd450Log], 0);
        if (exponential) {
            // Preserve the existing exp(NaN) = 1 compatibility behavior.
            EXPECT_GE(nan_checks, 1);
            EXPECT_GE(nan_selects, 1);
        }
    }
}

uint64_t encode(std::string_view pattern, const std::map<char, uint64_t> &fields) {
    uint64_t result = 0;
    std::map<char, unsigned> positions;
    for (size_t bit = 0; bit < pattern.size(); ++bit) {
        const char field = pattern[pattern.size() - 1 - bit];
        if (field == '1')
            result |= uint64_t{ 1 } << bit;
        else if (field != '0' && field != '-') {
            const auto value = fields.find(field);
            if (value != fields.end())
                result |= ((value->second >> positions[field]) & 1) << bit;
            ++positions[field];
        }
    }
    return result;
}

unsigned count_op(const shader::usse::SpirvCode &code, spv::Op opcode) {
    unsigned count = 0;
    for (size_t i = 5; i < code.size(); i += code[i] >> 16)
        count += (code[i] & 0xffff) == opcode;
    return count;
}
} // namespace

TEST(complex_instructions, vcomp_base_two) {
    // VEXP pa0.x i1.x (F32), VEXP pa0.x pa0.x (F16).
    for (const uint64_t instruction : { 0x30800e0200003e81ull, 0x30a00e8280000001ull }) {
        check_complex(instruction, GLSLstd450Exp2, true);
        // VCOMP op2: 3 = EXP, 2 = LOG.
        check_complex(instruction & ~(1ull << 41), GLSLstd450Log2, false);
    }
}

TEST(complex_instructions, dual_base_two) {
    // FEXP i2.x i0.x + FMUL pa0.xy pa2.x i1.x; exercise F16 and F32.
    for (const uint64_t instruction : { 0x20604802a0091082ull, 0x20404802a0091082ull }) {
        check_complex(instruction, GLSLstd450Exp2, true);
        // Dual op1: 12 = FEXP, 13 = FLOG.
        check_complex(instruction | (1ull << 44), GLSLstd450Log2, false);
    }
}

TEST(complex_instructions, partial_vpck_clears_high_half_only_when_enabled) {
    // Captured Killzone instruction: VPCKU16U8 r0.x pa4.x. Its next
    // instruction consumes the entire r0 word as an integer address.
    constexpr uint64_t instruction = 0x408100c4a0000200ull;
    FeatureState enabled{};
    enabled.killzone_vpck_workaround = true;
    const auto count_zero_high_half = [](const shader::usse::SpirvCode &code) {
        std::map<unsigned, bool> integer_types, integer_pairs, zero_constants;
        unsigned count = 0;
        for (size_t offset = 5; offset < code.size(); offset += code[offset] >> 16) {
            const auto op = code[offset] & 0xffff;
            if (op == spv::OpTypeInt)
                integer_types[code[offset + 1]] = true;
            if (op == spv::OpTypeVector)
                integer_pairs[code[offset + 1]] = integer_types[code[offset + 2]] && code[offset + 3] == 2;
            if (op == spv::OpConstant && integer_types[code[offset + 1]])
                zero_constants[code[offset + 2]] = code[offset + 3] == 0;
            if (op == spv::OpCompositeConstruct && integer_pairs[code[offset + 1]] && zero_constants[code[offset + 4]])
                ++count;
        }
        return count;
    };
    const auto original = ComplexFixture(instruction).compile(shader::Target::SpirVVulkan);
    const auto fixed = ComplexFixture(instruction).compile(shader::Target::SpirVVulkan, enabled);
    EXPECT_EQ(count_zero_high_half(original), 0);
    EXPECT_EQ(count_zero_high_half(fixed), 1);

    // Filling the entire packed word must retain exactly the old translation.
    // VPCK's destination mask occupies bits 34..37; change x to xy.
    const ComplexFixture full_word(instruction | (2ull << 34));
    EXPECT_EQ(full_word.compile(shader::Target::SpirVVulkan), full_word.compile(shader::Target::SpirVVulkan, enabled));
}

TEST(complex_instructions, dual_scalar_results_fill_all_destination_components) {
    // Captured Killzone instructions. Internal registers store F32 even when
    // the instruction uses F16 operands. A scalar store only updates x, so
    // these xyz destinations require an explicit three-component broadcast.
    const std::array<uint64_t, 3> instructions{
        0x20a461009f861081ull, // FRSQ i1.x i1.x + VDP i0.xyz i0.xyz pa2.xyz
        0x20214100efb9138cull, // VADD i2.xyz i0.xyz sa24.yyy + FRSQ i1.xyz i1.x
        0x20c048009f8a0886ull, // FEXP i1.x i0.x + FMUL i0.xyz i2.x pa6.x
    };
    for (const auto target : { shader::Target::SpirVOpenGL, shader::Target::SpirVVulkan }) {
        for (const auto instruction : instructions) {
            SCOPED_TRACE(instruction);
            const auto code = ComplexFixture(instruction).compile(target);
            unsigned broadcasts = 0;
            for (size_t offset = 5; offset < code.size(); offset += code[offset] >> 16) {
                const auto words = code[offset] >> 16;
                if ((code[offset] & 0xffff) == spv::OpCompositeConstruct && words == 6
                    && code[offset + 3] == code[offset + 4] && code[offset + 4] == code[offset + 5])
                    ++broadcasts;
            }
            EXPECT_EQ(broadcasts, 1);
        }
    }
}

TEST(complex_instructions, fifa_imad16_subtracts_on_every_repeat) {
    constexpr std::string_view pattern = "10100ppasnredbck-tttmmffoolhhgiijjqquuuuuuuvvvvvvvwwwwwwwxxxxxxx";
    for (const auto target : { shader::Target::SpirVOpenGL, shader::Target::SpirVVulkan }) {
        for (const unsigned repeats : { 0u, 2u, 3u }) {
            const auto code = ComplexFixture(encode(pattern, { { 'r', 1 }, { 't', repeats } })).compile(target);
            // r0 = src0 * src1 - src2, repeated over distinct register slots.
            EXPECT_EQ(count_op(code, spv::OpIMul), repeats + 1);
            EXPECT_EQ(count_op(code, spv::OpISub), repeats + 1);
        }
    }
}

TEST(complex_instructions, gundam_vmad2_decodes_short_predicates) {
    using namespace shader::usse;
    const std::array expected{ ExtPredicate::NONE, ExtPredicate::P0, ExtPredicate::NEGP0, ExtPredicate::PN };
    for (unsigned i = 0; i < expected.size(); ++i)
        EXPECT_EQ(get_predicate(uint64_t{ i } << 56), static_cast<uint8_t>(expected[i]));
    // Adding NEGP2 must preserve the encoded PN value of normal instructions.
    EXPECT_EQ(static_cast<uint8_t>(ExtPredicate::PN), 7);
    EXPECT_EQ(ext_vec_predicate_to_ext(ExtVecPredicate::NEGP2), ExtPredicate::NEGP2);
}

TEST(complex_instructions, super_hero_float_test_masks_contain_all_bits) {
    constexpr std::string_view pattern = "01111ppps-oydtrcevuuiizzm-aa--bbnnkkfffffffwllgggghhhhhhhjjjjjjj";
    for (const auto target : { shader::Target::SpirVOpenGL, shader::Target::SpirVVulkan }) {
        for (const unsigned precision : { 0u, 1u }) {
            const auto code = ComplexFixture(encode(pattern, { { 'e', precision }, { 'w', 1 }, { 'g', 2 } })).compile(target);
            const uint32_t expected = precision ? 0xffffffffu : 0xffffu;
            std::map<uint32_t, bool> integers;
            bool found_mask = false;
            for (size_t i = 5; i < code.size(); i += code[i] >> 16) {
                const auto op = code[i] & 0xffff;
                if (op == spv::OpTypeInt)
                    integers[code[i + 1]] = true;
                if (op == spv::OpConstant && integers[code[i + 1]] && code[i + 3] == expected)
                    found_mask = true;
            }
            EXPECT_TRUE(found_mask);
            EXPECT_GE(count_op(code, spv::OpSelect), 1);
        }
    }
}

TEST(complex_instructions, motogp_half_conversion_saturates_without_losing_nan) {
    for (const auto target : { shader::Target::SpirVOpenGL, shader::Target::SpirVVulkan }) {
        const auto code = ComplexFixture(0x30a00e8280000001ull).compile(target);
        bool has_clamp = false, has_pack = false;
        for (size_t i = 5; i < code.size(); i += code[i] >> 16) {
            if ((code[i] & 0xffff) != spv::OpExtInst)
                continue;
            has_clamp |= code[i + 4] == GLSLstd450FClamp;
            has_pack |= code[i + 4] == GLSLstd450PackHalf2x16;
        }
        EXPECT_TRUE(has_clamp);
        EXPECT_TRUE(has_pack);
        EXPECT_GE(count_op(code, spv::OpIsNan), 2); // EXP compatibility plus F16 packing
        EXPECT_GE(count_op(code, spv::OpSelect), 2);
    }
}

TEST(complex_instructions, uppers_allocates_declared_temporary_registers) {
    ComplexFixture fixture(0x30800e0200003e81ull);
    fixture.program.temp_reg_count1 = 300;
    const auto code = fixture.compile(shader::Target::SpirVVulkan);
    spirv_cross::Compiler compiler(code);
    uint32_t r_id = 0;
    for (size_t i = 5; i < code.size(); i += code[i] >> 16) {
        if ((code[i] & 0xffff) == spv::OpVariable && compiler.get_name(code[i + 2]) == "r")
            r_id = code[i + 2];
    }
    ASSERT_NE(r_id, 0);
    const auto &type = compiler.get_type_from_variable(r_id);
    ASSERT_EQ(type.array.size(), 1);
    EXPECT_EQ(type.array[0], 75);
}
