// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later
#define VMA_IMPLEMENTATION
#include <vkutil/objects.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <set>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

// Image transitions are not exercised by this buffer-only executable.
namespace vkutil {
void transition_image_layout(vk::CommandBuffer, vk::Image, ImageLayout, ImageLayout, const vk::ImageSubresourceRange &) { std::abort(); }
void transition_image_layout_discard(vk::CommandBuffer, vk::Image, ImageLayout, ImageLayout, const vk::ImageSubresourceRange &) { std::abort(); }
}

int main(int argc, char **argv) {
    assert(argc == 2); // Vulkan loader, or a directly loadable MoltenVK library.
    vk::detail::DynamicLoader loader(argv[1]);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
    const char *extensions[] = { "VK_KHR_portability_enumeration" };
    vk::ApplicationInfo app{ .pApplicationName = "Frame upload regression", .apiVersion = VK_API_VERSION_1_1 };
    vk::InstanceCreateInfo instance_info{ .flags = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR, .pApplicationInfo = &app,
        .enabledExtensionCount = 1, .ppEnabledExtensionNames = extensions };
    const auto available_extensions = vk::enumerateInstanceExtensionProperties();
    if (std::none_of(available_extensions.begin(), available_extensions.end(), [](const auto &extension) {
            return std::strcmp(extension.extensionName, "VK_KHR_portability_enumeration") == 0;
        })) {
        instance_info.flags = {};
        instance_info.enabledExtensionCount = 0;
    }
    auto instance = vk::createInstance(instance_info);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);
    auto physical = instance.enumeratePhysicalDevices().at(0);
    std::cout << "GPU: " << physical.getProperties().deviceName << "\n";
    auto families = physical.getQueueFamilyProperties();
    uint32_t family = 0;
    while (!(families.at(family).queueFlags & vk::QueueFlagBits::eGraphics)) ++family;
    float priority = 1;
    vk::DeviceQueueCreateInfo queue_info{ .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority };
    const char *device_extensions[] = { "VK_KHR_portability_subset" };
    vk::DeviceCreateInfo device_info{ .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue_info,
        .enabledExtensionCount = 1, .ppEnabledExtensionNames = device_extensions };
    auto device = physical.createDevice(device_info);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(device);
    auto queue = device.getQueue(family, 0);
    vma::VulkanFunctions functions{ .vkGetInstanceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr };
    vma::AllocatorCreateInfo allocator_info{ .physicalDevice = physical, .device = device,
        .pVulkanFunctions = &functions, .instance = instance, .vulkanApiVersion = VK_API_VERSION_1_1 };
    auto allocator = vma::createAllocator(allocator_info);
    vkutil::init(allocator);
    auto pool = device.createCommandPool(vk::CommandPoolCreateInfo{ .queueFamilyIndex = family });
    {
        // Tiny pages reproduce multiple wraps with ordinary data, including
        // an upload larger than an entire page and three pending frame slots.
        vkutil::HostFrameBuffer uploads(vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferSrc, 256, 3);
        vkutil::Buffer readback(16384);
        readback.init_buffer(vk::BufferUsageFlagBits::eTransferDst, vkutil::vma_mapped_alloc);
        std::set<VkBuffer> handles;
        size_t warm_handles = 0;
        for (unsigned cycle = 0; cycle < 3; ++cycle) {
            auto cmd = device.allocateCommandBuffers(vk::CommandBufferAllocateInfo{ .commandPool = pool, .commandBufferCount = 1 }).at(0);
            cmd.begin(vk::CommandBufferBeginInfo{ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
            std::vector<uint8_t> expected;
            for (unsigned frame = 0; frame < 3; ++frame) {
                uploads.begin_frame(frame);
                for (unsigned i = 0; i < 8; ++i) {
                    const size_t size = i == 4 ? 1024 : 64 + (i % 3) * 4;
                    std::vector<uint8_t> bytes(size, uint8_t(1 + cycle * 40 + frame * 8 + i));
                    uploads.allocate(cmd, bytes.size(), bytes.data());
                    assert(uploads.data_offset % uploads.alignment == 0);
                    handles.insert(uploads.handle());
                    vk::BufferCopy copy{ .srcOffset = uploads.data_offset, .dstOffset = expected.size(), .size = size };
                    cmd.copyBuffer(uploads.handle(), readback.buffer, copy);
                    expected.insert(expected.end(), bytes.begin(), bytes.end());
                }
            }
            // Only submit after ALL uploads, so an unsafe wrap corrupts the
            // early copies exactly like the captured Atelier Sophie frame.
            const vk::MemoryBarrier host_read{ .srcAccessMask = vk::AccessFlagBits::eTransferWrite, .dstAccessMask = vk::AccessFlagBits::eHostRead };
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost, {}, host_read, {}, {});
            cmd.end();
            vk::SubmitInfo submit{};
            submit.setCommandBuffers(cmd);
            queue.submit(submit);
            queue.waitIdle();
            allocator.invalidateAllocation(readback.allocation, 0, expected.size());
            assert(std::memcmp(readback.mapped_data, expected.data(), expected.size()) == 0);
            device.resetCommandPool(pool);
            if (cycle == 0) warm_handles = handles.size();
            else assert(handles.size() == warm_handles); // Pages are reused after completion.
        }
        assert(handles.size() > 3);
        std::cout << "PASS: deferred GPU reads survive page overflow, oversized uploads, three frame slots and reuse (" << handles.size() << " pages)\n";
    }
    device.destroyCommandPool(pool);
    vkutil::deinit();
    allocator.destroy();
    device.destroy();
    instance.destroy();
}
