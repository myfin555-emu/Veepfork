// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later

#include <renderer/shaders.h>
#include <renderer/state.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <fstream>

namespace {
// Exercise disk-cache validation without creating a window or GPU device.
struct CacheState : renderer::State {
    uint32_t mask = 1;
    bool init() override { return true; }
    void late_init(const Config &, std::string_view, MemState &) override {}
    renderer::TextureCache *get_texture_cache() override { return nullptr; }
    void render_frame(DisplayState &, const GxmState &, MemState &) override {}
    void swap_window() override {}
    std::vector<uint32_t> dump_frame(DisplayState &, uint32_t &, uint32_t &) override { return {}; }
    uint32_t get_features_mask() override { return mask; }
    int get_supported_filters() override { return 0; }
    void set_screen_filter(const std::string_view &) override {}
    int get_max_anisotropic_filtering() override { return 1; }
    void set_anisotropic_filtering(int) override {}
    int get_max_2d_texture_width() override { return 0; }
    std::string_view get_gpu_name() override { return "cache-test"; }
    void precompile_shader(const renderer::ShadersHash &) override {}
    void preclose_action() override {}
};

class ShaderCache : public testing::Test {
protected:
    fs::path root = fs::temp_directory_path() / fs::unique_path("vita3k-shader-cache-%%%%-%%%%");
    CacheState state;
    void SetUp() override {
        state.current_backend = renderer::Backend::OpenGL;
        state.shaders_path = root / "cache";
        state.shaders_log_path = root / "logs";
        fs::create_directories(state.shaders_path);
        fs::create_directories(state.shaders_log_path);
        std::ofstream((state.shaders_path / "orphan.spv").string()) << "old features";
        std::ofstream((state.shaders_log_path / "orphan.gxp").string()) << "old dump";
    }
    void TearDown() override { fs::remove_all(root); }
    void save_index() { renderer::save_shaders_cache_hashs(state, state.shaders_cache_hashs); }
    void expect_invalidated() {
        EXPECT_FALSE(renderer::get_shaders_cache_hashs(state));
        EXPECT_FALSE(fs::exists(state.shaders_path / "orphan.spv"));
        EXPECT_FALSE(fs::exists(state.shaders_log_path / "orphan.gxp"));
        EXPECT_TRUE(state.shaders_cache_hashs.empty());
    }
};

struct FragmentFixture {
    SceGxmProgram program{};
    SceGxmProgramVertexVaryings varyings{};
    uint64_t instructions = 0;
    FragmentFixture() {
        program.magic = 0x00505847;
        program.major_version = 1;
        program.minor_version = 4;
        program.size = sizeof(*this);
        program.program_flags = SCE_GXM_PROGRAM_FLAG_FRAGMENT | SCE_GXM_PROGRAM_FLAG_NATIVECOLOR_USED;
        program.primary_reg_count = 8;
        program.varyings_offset = offsetof(FragmentFixture, varyings) - offsetof(SceGxmProgram, varyings_offset);
        program.primary_program_offset = offsetof(FragmentFixture, instructions) - offsetof(SceGxmProgram, primary_program_offset);
        program.secondary_program_offset = offsetof(FragmentFixture, instructions) - offsetof(SceGxmProgram, secondary_program_offset);
        program.secondary_program_offset_end = offsetof(FragmentFixture, instructions) - offsetof(SceGxmProgram, secondary_program_offset_end);
        varyings.output_param_type = SCE_GXM_PARAMETER_TYPE_F16;
        varyings.output_comp_count = 4;
    }
};
} // namespace

TEST_F(ShaderCache, missing_index_rejects_orphan_shaders) {
    expect_invalidated();
}

TEST_F(ShaderCache, truncated_index_rejects_orphan_shaders) {
    std::ofstream((state.shaders_path / "hashs-gl.dat").string()) << "bad";
    expect_invalidated();
}

TEST_F(ShaderCache, changed_features_reject_previous_shaders) {
    save_index();
    state.mask = 3; // texture viewport enabled
    expect_invalidated();
}

TEST_F(ShaderCache, complete_empty_index_retains_compatible_shaders) {
    save_index();
    EXPECT_FALSE(renderer::get_shaders_cache_hashs(state));
    EXPECT_TRUE(fs::exists(state.shaders_path / "orphan.spv"));
}

TEST_F(ShaderCache, complete_index_restores_shader_hashes) {
    renderer::ShadersHash entry{};
    entry.frag[0] = 1;
    entry.vert[31] = 2;
    state.shaders_cache_hashs.push_back(entry);
    save_index();
    state.shaders_cache_hashs.clear();
    ASSERT_TRUE(renderer::get_shaders_cache_hashs(state));
    ASSERT_EQ(state.shaders_cache_hashs.size(), 1);
    EXPECT_EQ(state.shaders_cache_hashs[0].frag, entry.frag);
    EXPECT_EQ(state.shaders_cache_hashs[0].vert, entry.vert);
    EXPECT_TRUE(fs::exists(state.shaders_path / "orphan.spv"));
}

TEST_F(ShaderCache, truncated_hash_list_rejects_cache) {
    state.shaders_cache_hashs.resize(2);
    save_index();
    const auto index = state.shaders_path / "hashs-gl.dat";
    fs::resize_file(index, fs::file_size(index) - 1);
    expect_invalidated();
}

TEST_F(ShaderCache, output_register_variants_survive_warm_cache) {
    FragmentFixture fixture;
    shader::Hints hints{};
    hints.color_format = SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR;
    const auto compile = [&](const fs::path &cache) {
        return renderer::load_spirv_shader(fixture.program, FeatureState{}, true, hints, false,
            cache, state.shaders_log_path, "test", true);
    };
    const auto declared = compile(state.shaders_path);
    hints.output_register_format = SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4;
    const auto packed_cold = compile(root / "cold");
    ASSERT_NE(declared, packed_cold);
    EXPECT_EQ(compile(state.shaders_path), packed_cold);
    hints.output_register_format = SCE_GXM_OUTPUT_REGISTER_FORMAT_DECLARED;
    EXPECT_EQ(compile(state.shaders_path), declared);
}

TEST_F(ShaderCache, fragment_hint_does_not_change_vertex_identity) {
    SceGxmProgram vertex{};
    vertex.size = sizeof(vertex);
    EXPECT_EQ(renderer::get_shader_hash(vertex), sha256(&vertex, sizeof(vertex)));
    EXPECT_EQ(renderer::get_shader_hash(vertex, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4),
        renderer::get_shader_hash(vertex));
}
