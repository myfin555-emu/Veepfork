// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later
#define VMA_IMPLEMENTATION
#include <vkutil/objects.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace vkutil {
void transition_image_layout(vk::CommandBuffer, vk::Image, ImageLayout, ImageLayout, const vk::ImageSubresourceRange &) { std::abort(); }
void transition_image_layout_discard(vk::CommandBuffer, vk::Image, ImageLayout, ImageLayout, const vk::ImageSubresourceRange &) { std::abort(); }
}

static vk::ShaderModule load_shader(vk::Device device, const char *path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    assert(input.good());
    const size_t size = input.tellg();
    assert(size && size % 4 == 0);
    std::vector<uint32_t> code(size / 4);
    input.seekg(0);
    input.read(reinterpret_cast<char *>(code.data()), size);
    return device.createShaderModule({ .codeSize = size, .pCode = code.data() });
}

int main(int argc, char **argv) {
    assert(argc == 4); // Loader, fullscreen vertex SPIR-V, fragment SPIR-V.
    vk::detail::DynamicLoader loader(argv[1]);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
    const char *extensions[] = { "VK_KHR_portability_enumeration" };
    vk::ApplicationInfo app{ .pApplicationName = "Visibility readback regression", .apiVersion = VK_API_VERSION_1_1 };
    vk::InstanceCreateInfo instance_info{ .flags = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR,
        .pApplicationInfo = &app, .enabledExtensionCount = 1, .ppEnabledExtensionNames = extensions };
    auto available = vk::enumerateInstanceExtensionProperties();
    if (std::none_of(available.begin(), available.end(), [](const auto &extension) {
            return std::strcmp(extension.extensionName, "VK_KHR_portability_enumeration") == 0;
        })) {
        instance_info.flags = {};
        instance_info.enabledExtensionCount = 0;
    }
    auto instance = vk::createInstance(instance_info);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);
    auto physical = instance.enumeratePhysicalDevices().at(0);
    std::cout << "GPU: " << physical.getProperties().deviceName << std::endl;
    assert(physical.getFeatures().occlusionQueryPrecise);
    vk::PhysicalDeviceFeatures features{ .occlusionQueryPrecise = true };
    auto families = physical.getQueueFamilyProperties();
    uint32_t family = 0;
    while (!(families.at(family).queueFlags & vk::QueueFlagBits::eGraphics)) ++family;
    float priority = 1;
    vk::DeviceQueueCreateInfo queue_info{ .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority };
    const char *device_extensions[] = { "VK_KHR_portability_subset" };
    auto device = physical.createDevice(vk::DeviceCreateInfo{ .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue_info,
        .enabledExtensionCount = 1, .ppEnabledExtensionNames = device_extensions, .pEnabledFeatures = &features });
    VULKAN_HPP_DEFAULT_DISPATCHER.init(device);
    auto queue = device.getQueue(family, 0);
    vma::VulkanFunctions functions{ .vkGetInstanceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr };
    auto allocator = vma::createAllocator(vma::AllocatorCreateInfo{ .physicalDevice = physical, .device = device,
        .pVulkanFunctions = &functions, .instance = instance, .vulkanApiVersion = VK_API_VERSION_1_1 });
    vkutil::init(allocator);
    auto pool = device.createCommandPool({ .queueFamilyIndex = family });
    {
        constexpr uint32_t width = 32, slots = 8;
        vkutil::Image color(width, width, vk::Format::eR8G8B8A8Unorm);
        color.init_image(vk::ImageUsageFlagBits::eColorAttachment);
        const vk::AttachmentDescription attachment{ .format = color.format, .samples = vk::SampleCountFlagBits::e1,
            .loadOp = vk::AttachmentLoadOp::eClear, .storeOp = vk::AttachmentStoreOp::eStore,
            .initialLayout = vk::ImageLayout::eUndefined, .finalLayout = vk::ImageLayout::eColorAttachmentOptimal };
        const vk::AttachmentReference reference{ .attachment = 0, .layout = vk::ImageLayout::eColorAttachmentOptimal };
        const vk::SubpassDescription subpass{ .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
            .colorAttachmentCount = 1, .pColorAttachments = &reference };
        auto render_pass = device.createRenderPass({ .attachmentCount = 1, .pAttachments = &attachment,
            .subpassCount = 1, .pSubpasses = &subpass });
        auto framebuffer = device.createFramebuffer({ .renderPass = render_pass, .attachmentCount = 1,
            .pAttachments = &color.view, .width = width, .height = width, .layers = 1 });
        auto vertex = load_shader(device, argv[2]), fragment = load_shader(device, argv[3]);
        const vk::PipelineShaderStageCreateInfo stages[] = {
            { .stage = vk::ShaderStageFlagBits::eVertex, .module = vertex, .pName = "main" },
            { .stage = vk::ShaderStageFlagBits::eFragment, .module = fragment, .pName = "main" }
        };
        const vk::PipelineVertexInputStateCreateInfo vertex_input{};
        const vk::PipelineInputAssemblyStateCreateInfo assembly{ .topology = vk::PrimitiveTopology::eTriangleList };
        const vk::Viewport viewport{ .width = width, .height = width, .maxDepth = 1 };
        const vk::Rect2D scissor{ .extent = { width, width } };
        const vk::PipelineViewportStateCreateInfo viewport_state{ .viewportCount = 1, .pViewports = &viewport,
            .scissorCount = 1, .pScissors = &scissor };
        const vk::PipelineRasterizationStateCreateInfo raster{ .polygonMode = vk::PolygonMode::eFill, .lineWidth = 1 };
        const vk::PipelineMultisampleStateCreateInfo multisample{ .rasterizationSamples = vk::SampleCountFlagBits::e1 };
        const vk::PipelineColorBlendAttachmentState blend_attachment{ .colorWriteMask = vk::ColorComponentFlagBits::eR };
        const vk::PipelineColorBlendStateCreateInfo blend{ .attachmentCount = 1, .pAttachments = &blend_attachment };
        auto layout = device.createPipelineLayout({});
        auto pipeline = device.createGraphicsPipeline({}, vk::GraphicsPipelineCreateInfo{ .stageCount = 2, .pStages = stages,
            .pVertexInputState = &vertex_input, .pInputAssemblyState = &assembly, .pViewportState = &viewport_state,
            .pRasterizationState = &raster, .pMultisampleState = &multisample, .pColorBlendState = &blend,
            .layout = layout, .renderPass = render_pass }).value;
        auto queries = device.createQueryPool({ .queryType = vk::QueryType::eOcclusion, .queryCount = slots });
        vkutil::Buffer readback(slots * sizeof(uint64_t));
        readback.init_buffer(vk::BufferUsageFlagBits::eTransferDst, vkutil::vma_mapped_alloc);
        // Include empty, adjacent, sparse and final-slot queries. Exact counts
        // distinguish increment mode from a boolean "visible" approximation.
        const std::array<uint32_t, 4> indices{ 0, 2, 3, slots - 1 };
        const std::array<std::pair<uint32_t, uint32_t>, 3> ranges{ { { 0, 1 }, { 2, 2 }, { slots - 1, 1 } } };
        for (unsigned cycle = 0; cycle < 12; ++cycle) {
            std::array<uint32_t, slots * 4> guest;
            guest.fill(0xdeadbeef);
            auto expected = guest;
            auto commands = device.allocateCommandBuffers({ .commandPool = pool, .commandBufferCount = 2 });
            auto before = commands[0], render = commands[1];
            before.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
            render.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
            vk::ClearValue clear{ .color = { .float32 = std::array<float, 4>{ 0, 0, 0, 1 } } };
            render.beginRenderPass(vk::RenderPassBeginInfo{ .renderPass = render_pass, .framebuffer = framebuffer,
                .renderArea = scissor, .clearValueCount = 1, .pClearValues = &clear }, vk::SubpassContents::eInline);
            render.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
            for (uint32_t i = 0; i < indices.size(); ++i) {
                const uint32_t count = i == 0 ? 0 : i + cycle % 3;
                render.beginQuery(queries, indices[i], vk::QueryControlFlagBits::ePrecise);
                for (uint32_t draw = 0; draw < count; ++draw) render.draw(3, 1, 0, 0);
                render.endQuery(queries, indices[i]);
                expected[indices[i]] = width * width * count;
            }
            render.endRenderPass();
            for (const auto &[offset, count] : ranges) {
                before.resetQueryPool(queries, offset, count);
                render.copyQueryPoolResults(queries, offset, count, readback.buffer,
                    offset * sizeof(uint64_t), sizeof(uint64_t), vk::QueryResultFlagBits::eWait | vk::QueryResultFlagBits::e64);
            }
            const vk::MemoryBarrier host_read{ .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
                .dstAccessMask = vk::AccessFlagBits::eHostRead };
            render.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost,
                {}, host_read, {}, {});
            before.end();
            render.end();
            auto fence = device.createFence({});
            vk::SubmitInfo submit{};
            submit.setCommandBuffers(commands);
            queue.submit(submit, fence);
            assert(device.waitForFences(fence, true, 10'000'000'000ULL) == vk::Result::eSuccess);
            for (const auto &[offset, count] : ranges) {
                allocator.invalidateAllocation(readback.allocation, offset * sizeof(uint64_t), count * sizeof(uint64_t));
                for (uint32_t i = offset; i < offset + count; ++i)
                    guest[i] = static_cast<uint32_t>(static_cast<const uint64_t *>(readback.mapped_data)[i]);
            }
            if (guest != expected) {
                for (auto index : indices) {
                    uint32_t direct = 0;
                    auto result = device.getQueryPoolResults(queries, index, 1, sizeof(direct), &direct,
                        sizeof(direct), vk::QueryResultFlagBits::eWait);
                    std::cerr << "query " << index << " direct=" << direct << " result=" << int(result) << '\n';
                }
                for (unsigned i = 0; i < slots; ++i)
                    std::cerr << "staging " << i << ": " << static_cast<const uint64_t *>(readback.mapped_data)[i] << '\n';
                for (unsigned i = 0; i < guest.size(); ++i)
                    std::cerr << i << ": " << guest[i] << " expected " << expected[i] << '\n';
                std::abort();
            }
            device.destroyFence(fence);
            device.freeCommandBuffers(pool, commands);
            device.resetCommandPool(pool);
        }
        std::cout << "PASS: 12 GPU submissions, exact increment counts, empty query, sparse/final slots, pool reuse; untouched slots and other cores preserved" << std::endl;
        device.destroyQueryPool(queries);
        device.destroyPipeline(pipeline);
        device.destroyPipelineLayout(layout);
        device.destroyShaderModule(vertex);
        device.destroyShaderModule(fragment);
        device.destroyFramebuffer(framebuffer);
        device.destroyRenderPass(render_pass);
    }
    device.destroyCommandPool(pool);
    vkutil::deinit();
    allocator.destroy();
    device.destroy();
    instance.destroy();
}
