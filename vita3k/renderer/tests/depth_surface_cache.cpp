// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later

#include <mem/state.h>
#include <renderer/vulkan/state.h>
#include <vkutil/vkutil.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace renderer::vulkan;

static void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

static vk::ShaderModule load_shader(vk::Device device, const char *path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(input.good(), "Cannot open test shader");
    const size_t size = input.tellg();
    require(size && size % 4 == 0, "Invalid test shader size");
    std::vector<uint32_t> code(size / 4);
    input.seekg(0);
    input.read(reinterpret_cast<char *>(code.data()), size);
    return device.createShaderModule({ .codeSize = size, .pCode = code.data() });
}

// Exercise the production cache and sample its returned views on a real GPU.
// No guest memory, firmware, window, game-specific workaround or JIT is needed.
class DepthCacheTest {
    vk::detail::DynamicLoader loader;
    MemState mem;
    std::unique_ptr<VKState> state;
    std::unique_ptr<VKContext> context;
    std::unique_ptr<VKRenderTarget> target;
    vk::DescriptorSetLayout sample_set_layout;
    vk::PipelineLayout sample_layout;
    vk::DescriptorPool sample_pool;
    vk::DescriptorSet sample_set;
    vk::Sampler sampler;
    std::array<vk::Pipeline, 2> pipelines;

    struct Params {
        std::array<float, 4> viewport;
        uint32_t width, height;
    };

public:
    DepthCacheTest(const char *depth_shader, const char *stencil_shader)
        : loader(std::getenv("VULKAN_LOADER") ? std::getenv("VULKAN_LOADER") : "")
        , state(std::make_unique<VKState>(0)) {
        VULKAN_HPP_DEFAULT_DISPATCHER.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
        const char *portability = "VK_KHR_portability_enumeration";
        const auto extensions = vk::enumerateInstanceExtensionProperties();
        const bool portable = std::any_of(extensions.begin(), extensions.end(), [&](const auto &e) {
            return std::strcmp(e.extensionName, portability) == 0;
        });
        const vk::ApplicationInfo app{ .pApplicationName = "Depth surface cache regression", .apiVersion = VK_API_VERSION_1_1 };
        state->instance = vk::createInstance({ .flags = portable ? vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR : vk::InstanceCreateFlags{},
            .pApplicationInfo = &app,
            .enabledExtensionCount = portable ? 1U : 0U,
            .ppEnabledExtensionNames = &portability });
        VULKAN_HPP_DEFAULT_DISPATCHER.init(state->instance);
        state->physical_device = state->instance.enumeratePhysicalDevices().at(0);
        state->physical_device_properties = state->physical_device.getProperties();
        std::cout << "GPU: " << state->physical_device_properties.deviceName << '\n';
        const auto families = state->physical_device.getQueueFamilyProperties();
        uint32_t family = 0;
        const auto required_queues = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute;
        while ((families.at(family).queueFlags & required_queues) != required_queues)
            ++family;
        const float priority = 1.0f;
        const vk::DeviceQueueCreateInfo queue{ .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority };
        const char *subset = "VK_KHR_portability_subset";
        const auto device_extensions = state->physical_device.enumerateDeviceExtensionProperties();
        const bool has_subset = std::any_of(device_extensions.begin(), device_extensions.end(), [&](const auto &e) {
            return std::strcmp(e.extensionName, subset) == 0;
        });
        state->device = state->physical_device.createDevice({ .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue, .enabledExtensionCount = has_subset ? 1U : 0U, .ppEnabledExtensionNames = &subset });
        VULKAN_HPP_DEFAULT_DISPATCHER.init(state->device);
        state->general_queue = state->device.getQueue(family, 0);
        state->general_command_pool = state->device.createCommandPool({ .queueFamilyIndex = family });
        const vma::VulkanFunctions functions{ .vkGetInstanceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr,
            .vkGetDeviceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr };
        state->allocator = vma::createAllocator({ .physicalDevice = state->physical_device, .device = state->device, .pVulkanFunctions = &functions, .instance = state->instance, .vulkanApiVersion = VK_API_VERSION_1_1 });
        vkutil::init(state->allocator);
        state->deep_stencil_use = vk::Format::eD32SfloatS8Uint;
        state->res_multiplier = 1.0f;
        for (auto &frame : state->frames) {
            frame.render_pool = state->device.createCommandPool({ .queueFamilyIndex = family });
            frame.prerender_pool = state->device.createCommandPool({ .queueFamilyIndex = family });
            frame.destroy_queue.init(state->device);
        }

        // VKContext needs the normal uniform layouts but no graphics pipelines.
        std::array<vk::DescriptorSetLayoutBinding, 4> uniforms;
        for (uint32_t i = 0; i < uniforms.size(); ++i)
            uniforms[i] = { .binding = i, .descriptorType = i < 2 ? vk::DescriptorType::eUniformBufferDynamic : vk::DescriptorType::eStorageBufferDynamic, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eAllGraphics };
        state->pipeline_cache.uniforms_layout = state->device.createDescriptorSetLayout({ .bindingCount = 4, .pBindings = uniforms.data() });
        state->pipeline_cache.fragment_textures_layout[0] = state->device.createDescriptorSetLayout({});
        context = std::make_unique<VKContext>(*state, mem);
        state->context = context.get();
        SceGxmRenderTargetParams target_params{};
        target_params.width = 64;
        target_params.height = 64;
        target_params.scenesPerFrame = 1;
        target = std::make_unique<VKRenderTarget>(*state, target_params);

        const std::array<vk::DescriptorSetLayoutBinding, 2> bindings{ { { .binding = 0, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute },
            { .binding = 1, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute } } };
        sample_set_layout = state->device.createDescriptorSetLayout({ .bindingCount = 2, .pBindings = bindings.data() });
        const vk::PushConstantRange push{ .stageFlags = vk::ShaderStageFlagBits::eCompute, .size = sizeof(Params) };
        sample_layout = state->device.createPipelineLayout({ .setLayoutCount = 1, .pSetLayouts = &sample_set_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &push });
        const std::array<vk::DescriptorPoolSize, 2> sizes{ { { vk::DescriptorType::eCombinedImageSampler, 1 }, { vk::DescriptorType::eStorageBuffer, 1 } } };
        sample_pool = state->device.createDescriptorPool({ .maxSets = 1, .poolSizeCount = 2, .pPoolSizes = sizes.data() });
        sample_set = state->device.allocateDescriptorSets({ .descriptorPool = sample_pool, .descriptorSetCount = 1, .pSetLayouts = &sample_set_layout })[0];
        sampler = state->device.createSampler({ .magFilter = vk::Filter::eNearest, .minFilter = vk::Filter::eNearest, .addressModeU = vk::SamplerAddressMode::eClampToEdge, .addressModeV = vk::SamplerAddressMode::eClampToEdge });
        const char *shader_paths[] = { depth_shader, stencil_shader };
        for (size_t i = 0; i < pipelines.size(); ++i) {
            auto shader = load_shader(state->device, shader_paths[i]);
            pipelines[i] = state->device.createComputePipeline({}, vk::ComputePipelineCreateInfo{ .stage = { .stage = vk::ShaderStageFlagBits::eCompute, .module = shader, .pName = "main" }, .layout = sample_layout }).value;
            state->device.destroyShaderModule(shader);
        }
    }

    ~DepthCacheTest() {
        state->device.waitIdle();
        state->surface_cache.cleanup();
        target.reset();
        context.reset();
        for (auto pipeline : pipelines)
            state->device.destroyPipeline(pipeline);
        state->device.destroySampler(sampler);
        state->device.destroyDescriptorPool(sample_pool);
        state->device.destroyPipelineLayout(sample_layout);
        state->device.destroyDescriptorSetLayout(sample_set_layout);
        state->device.destroyDescriptorSetLayout(state->pipeline_cache.uniforms_layout);
        state->device.destroyDescriptorSetLayout(state->pipeline_cache.fragment_textures_layout[0]);
        for (auto &frame : state->frames) {
            frame.destroy_queue.destroy_objects();
            state->device.destroyCommandPool(frame.render_pool);
            state->device.destroyCommandPool(frame.prerender_pool);
        }
        state->device.destroyCommandPool(state->general_command_pool);
        const auto allocator = state->allocator;
        const auto device = state->device;
        const auto instance = state->instance;
        state.reset();
        vkutil::deinit();
        allocator.destroy();
        device.destroy();
        instance.destroy();
    }

    // The depth ramp encodes both axes, so shrinking, stretching, a bad copy
    // origin or stale cached contents cannot pass by sampling a uniform image.
    static float depth_at(uint32_t x, uint32_t y) { return float(1 + x + 256 * y) / 65536.0f; }
    static uint8_t stencil_at(uint32_t x, uint32_t y) { return 1 + (x + 7 * y) % 254; }

    void run(SceGxmMultisampleMode mode, bool downscale, float scale, bool viewport, bool feedback, bool stencil, bool padded, bool subregion) {
        std::cout << "case msaa=" << mode << " downscale=" << downscale << " scale=" << scale
                  << " viewport=" << viewport << " feedback=" << feedback << " stencil=" << stencil
                  << " padded=" << padded << " subregion=" << subregion << std::endl;
        state->surface_cache.cleanup();
        state->surface_cache.set_render_target(target.get());
        state->res_multiplier = scale;
        state->features.use_texture_viewport = viewport;
        context->record.color_surface.downscale = downscale;
        target->multisample_mode = mode;
        // Mirrors scene start's existing render target expansion. Expected
        // results below are read back as pixel values, not metadata only.
        const bool expanded = mode != SCE_GXM_MULTISAMPLE_NONE && !downscale;
        const uint32_t frame_width = expanded ? 128 : 64;
        const uint32_t frame_height = (padded ? 50 : 48) * (expanded ? 2 : 1);
        const uint32_t guest_width = mode == SCE_GXM_MULTISAMPLE_4X && downscale ? 128 : frame_width;
        const uint32_t guest_height = mode != SCE_GXM_MULTISAMPLE_NONE && downscale ? frame_height * 2 : frame_height;
        const uint32_t image_width = frame_width * scale, image_height = frame_height * scale;
        const uint32_t request_width = subregion ? guest_width - 16 : guest_width;
        const uint32_t request_height = subregion ? 16 : padded ? (guest_height + 31) / 32 * 32
                                                                : guest_height;
        const uint32_t guest_x = subregion ? 8 : 0, guest_y = subregion ? 16 : 0;
        const uint32_t output_width = request_width * image_width / guest_width;
        const uint32_t output_height = request_height * image_height / guest_height;
        const uint32_t origin_x = guest_x * image_width / guest_width, origin_y = guest_y * image_height / guest_height;

        auto cmd = state->device.allocateCommandBuffers({ .commandPool = state->general_command_pool, .commandBufferCount = 1 })[0];
        cmd.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
        context->prerender_cmd = cmd;
        ++context->scene_timestamp;
        SceGxmDepthStencilSurface surface{};
        surface.set_type(padded ? SCE_GXM_DEPTH_STENCIL_SURFACE_TILED : SCE_GXM_DEPTH_STENCIL_SURFACE_LINEAR);
        surface.set_format(SCE_GXM_DEPTH_STENCIL_FORMAT_DF32);
        surface.set_stride(guest_width);
        surface.depth_data = Ptr<void>(0x10000);
        surface.stencil_data = Ptr<void>(0x100000);
        auto attachment = state->surface_cache.retrieve_depth_stencil_for_framebuffer(&surface, image_width, image_height);
        context->current_ds_view = feedback ? attachment.view : vk::ImageView{};

        const size_t texels = image_width * image_height;
        vkutil::Buffer upload(texels * 5);
        upload.init_buffer(vk::BufferUsageFlagBits::eTransferSrc, vkutil::vma_mapped_alloc);
        auto *depth = static_cast<float *>(upload.mapped_data);
        auto *stencils = reinterpret_cast<uint8_t *>(depth + texels);
        for (uint32_t y = 0; y < image_height; ++y)
            for (uint32_t x = 0; x < image_width; ++x) {
                depth[y * image_width + x] = depth_at(x, y);
                stencils[y * image_width + x] = stencil_at(x, y);
            }
        state->allocator.flushAllocation(upload.allocation, 0, VK_WHOLE_SIZE);
        attachment.base_image->transition_to(cmd, vkutil::ImageLayout::TransferDst, vkutil::ds_subresource_range);
        std::array<vk::BufferImageCopy, 2> uploads{ { { .imageSubresource = { vk::ImageAspectFlagBits::eDepth, 0, 0, 1 }, .imageExtent = { image_width, image_height, 1 } },
            { .bufferOffset = texels * sizeof(float), .imageSubresource = { vk::ImageAspectFlagBits::eStencil, 0, 0, 1 }, .imageExtent = { image_width, image_height, 1 } } } };
        cmd.copyBufferToImage(upload.buffer, attachment.base_image->image, vk::ImageLayout::eTransferDstOptimal, uploads);
        attachment.base_image->transition_to(cmd, vkutil::ImageLayout::DepthStencilReadOnly, vkutil::ds_subresource_range);

        SceGxmTexture texture{};
        const uint32_t format = stencil ? SCE_GXM_TEXTURE_BASE_FORMAT_U8 : SCE_GXM_TEXTURE_BASE_FORMAT_F32;
        texture.base_format = (format >> 24) & 0x1f;
        texture.format0 = format >> 31;
        texture.width = request_width - 1;
        texture.height = request_height - 1;
        texture.type = (padded ? SCE_GXM_TEXTURE_TILED : SCE_GXM_TEXTURE_LINEAR_STRIDED) >> 29;
        const uint32_t bytes = stencil ? 1 : 4;
        texture.data_addr = ((stencil ? surface.stencil_data.address() : surface.depth_data.address()) + (guest_y * guest_width + guest_x) * bytes) >> 2;
        if (!padded) {
            const uint32_t stride = guest_width * bytes / 4 - 1;
            texture.mip_filter = stride & 1;
            texture.min_filter = (stride >> 1) & 3;
            texture.mip_count = (stride >> 3) & 15;
            texture.lod_bias = (stride >> 7) & 63;
        }

        TextureViewport texture_viewport{};
        texture_viewport.ratio = { 1.0f, 1.0f };
        if (padded) {
            auto outside = texture;
            outside.height += 32;
            require(!state->surface_cache.retrieve_depth_stencil_as_texture(outside, &texture_viewport), "Lookup accepted a texture past the allocation");
            auto padding_only = texture;
            padding_only.data_addr += guest_height * guest_width * bytes / 4;
            padding_only.height = 0;
            require(!state->surface_cache.retrieve_depth_stencil_as_texture(padding_only, &texture_viewport), "Lookup accepted a padding-only copy");
        } else if (!subregion) {
            auto outside = texture;
            outside.height += 1;
            require(!state->surface_cache.retrieve_depth_stencil_as_texture(outside, &texture_viewport), "Linear surface incorrectly gained tile padding");
        }
        // A later scene may use a different color downscale mode: lookup must
        // still use the sampling grid of the image that originally stored depth.
        context->record.color_surface.downscale = !downscale;
        auto result = state->surface_cache.retrieve_depth_stencil_as_texture(texture, &texture_viewport);
        require(result.has_value(), "Depth/stencil lookup missed the rendered surface");
        const auto second = state->surface_cache.retrieve_depth_stencil_as_texture(texture, &texture_viewport);
        require(second && second->view == result->view, "Repeated lookup did not reuse the view");

        vkutil::Buffer readback((output_width * output_height + 2) * sizeof(float));
        readback.init_buffer(vk::BufferUsageFlagBits::eStorageBuffer, vkutil::vma_mapped_alloc);
        const vk::DescriptorImageInfo source{ .sampler = sampler, .imageView = result->view, .imageLayout = result->layout == vkutil::ImageLayout::DepthStencilReadOnly ? vk::ImageLayout::eDepthStencilReadOnlyOptimal : vk::ImageLayout::eShaderReadOnlyOptimal };
        const vk::DescriptorBufferInfo destination{ .buffer = readback.buffer, .range = readback.size };
        const std::array<vk::WriteDescriptorSet, 2> writes{ { { .dstSet = sample_set, .dstBinding = 0, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &source },
            { .dstSet = sample_set, .dstBinding = 1, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eStorageBuffer, .pBufferInfo = &destination } } };
        state->device.updateDescriptorSets(writes, {});
        // The production cache synchronizes fragment sampling; this harness
        // samples from compute to read every texel without a window.
        const vk::MemoryBarrier sample_barrier{ .srcAccessMask = vk::AccessFlagBits::eTransferWrite, .dstAccessMask = vk::AccessFlagBits::eShaderRead };
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader, {}, sample_barrier, {}, {});
        const Params params{ { texture_viewport.offset.first, texture_viewport.offset.second, texture_viewport.ratio.first, texture_viewport.ratio.second }, output_width, output_height };
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipelines[stencil]);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, sample_layout, 0, sample_set, {});
        cmd.pushConstants(sample_layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(params), &params);
        cmd.dispatch((output_width + 7) / 8, (output_height + 7) / 8, 1);
        const vk::MemoryBarrier host_barrier{ .srcAccessMask = vk::AccessFlagBits::eShaderWrite, .dstAccessMask = vk::AccessFlagBits::eHostRead };
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eHost, {}, host_barrier, {}, {});
        cmd.end();
        const auto fence = state->device.createFence({});
        state->general_queue.submit(vk::SubmitInfo{ .commandBufferCount = 1, .pCommandBuffers = &cmd }, fence);
        require(state->device.waitForFences(fence, true, 10'000'000'000ULL) == vk::Result::eSuccess, "GPU submission timed out");
        state->allocator.invalidateAllocation(readback.allocation, 0, VK_WHOLE_SIZE);
        const auto *pixels = static_cast<const float *>(readback.mapped_data);
        const bool direct = !feedback && (viewport || (!subregion && !padded));
        require(pixels[0] == (direct ? image_width : output_width) && pixels[1] == (direct ? image_height : output_height), "Sampled image has the wrong dimensions");
        for (uint32_t y = 0; y < output_height; ++y)
            for (uint32_t x = 0; x < output_width; ++x) {
                const bool padding = x + origin_x >= image_width || y + origin_y >= image_height;
                const float expected = padding ? (stencil ? 0.0f : 1.0f) : stencil ? stencil_at(x + origin_x, y + origin_y)
                                                                                   : depth_at(x + origin_x, y + origin_y);
                if (pixels[2 + y * output_width + x] != expected) {
                    std::cerr << "Pixel (" << x << ',' << y << ") = " << pixels[2 + y * output_width + x] << ", expected " << expected << '\n';
                    throw std::runtime_error("Depth/stencil sampling changed the rendered pattern");
                }
            }
        state->device.destroyFence(fence);
        state->device.freeCommandBuffers(state->general_command_pool, cmd);
    }
};

int main(int argc, char **argv) {
    try {
        require(argc == 3, "Usage: renderer-depth-cache-tests depth.spv stencil.spv (optional VULKAN_LOADER)");
        DepthCacheTest test(argv[1], argv[2]);
        unsigned cases = 0;
        for (auto mode : { SCE_GXM_MULTISAMPLE_4X, SCE_GXM_MULTISAMPLE_NONE, SCE_GXM_MULTISAMPLE_2X })
            for (bool downscale : { false, true })
                for (float scale : { 1.0f, 1.5f, 2.0f })
                    for (bool stencil : { false, true }) {
                        // Full read, attachment feedback copy, offset copies,
                        // texture viewport offsets and a partial final tile.
                        test.run(mode, downscale, scale, true, false, stencil, false, false);
                        test.run(mode, downscale, scale, true, true, stencil, false, false);
                        test.run(mode, downscale, scale, false, false, stencil, false, true);
                        test.run(mode, downscale, scale, true, false, stencil, false, true);
                        test.run(mode, downscale, scale, false, false, stencil, true, false);
                        cases += 5;
                    }
        std::cout << "PASS: " << cases << " depth/stencil surface cache GPU cases\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
