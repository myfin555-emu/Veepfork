// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

// Step 6 of the iOS port (ios-plan.md): present a Vulkan/MoltenVK scene on
// the UIKit layer of the app. Everything here reuses the production paths:
//
//   A) Loader/dispatcher: the instance is initialized with the
//      vkGetInstanceProcAddr of the MoltenVK the app links statically
//      (XCFramework, no system loader exists on iOS — see VKState::create).
//   B) Surface: VK_EXT_metal_surface over the app's CAMetalLayer, delivered
//      by vita::ios::IOSFrameHost (renderer::IOSDisplayHandle). The main
//      (UIKit) thread only writes the pixel drawable size; the render thread
//      rebuilds the swapchain from it on resize and recreates the surface on
//      device/surface loss — the same machinery the emulator uses.
//   C) Scene: an animated 960x544 test pattern is written into guest memory
//      (the 4 GiB reservation validated in step 5) and presented through the
//      exact production frame path: upload as the "vita surface" + the
//      BilinearScreenFilter pipeline, whose shaders are the same
//      shaders-builtin/vulkan/*.spv binaries macOS uses, compiled to MSL by
//      MoltenVK.
//   D) The render loop runs on its own thread; nothing here touches UIKit.
//
// The homebrew frame of the acceptance criteria arrives with step 7 (first
// boot): the same upload/present path then carries the emulated front buffer
// instead of the test pattern.

#include <ios/bridge.h>
#include <ios/frame_host.h>
#include <ios/test_lock.h>

#include <config/state.h>
#include <display/state.h>
#include <gxm/state.h>
#include <mem/functions.h>
#include <mem/state.h>
#include <renderer/functions.h>
#include <renderer/state.h>
#include <renderer/vulkan/state.h>

#include <spdlog/spdlog.h>

#include <util/exit_code.h>
#include <util/fs.h>
#include <util/log.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

namespace {

constexpr uint32_t kPatternW = 960;
constexpr uint32_t kPatternH = 544;
constexpr uint32_t kPatternPitch = kPatternW; // RGBA8, no padding

// Animated test pattern: per-row color gradient sweeping over time plus a
// moving square. Row-wise math keeps the per-pixel loop integer-cheap; this
// runs on the render thread each frame (~0.5 Mpx).
void write_test_pattern(uint8_t *dst, uint32_t frame) {
    const float t = static_cast<float>(frame);
    const uint32_t sq = 96;
    for (uint32_t y = 0; y < kPatternH; y++) {
        const float fy = static_cast<float>(y) / static_cast<float>(kPatternH);
        const int32_t r = static_cast<int32_t>(127.f + 127.f * std::sin(fy * 6.28318f + t * 0.02f));
        const int32_t g = static_cast<int32_t>(127.f + 127.f * std::sin(fy * 12.56637f + t * 0.03f + 2.094f));
        const int32_t b = static_cast<int32_t>(127.f + 127.f * std::sin(fy * 3.14159f + t * 0.017f + 4.188f));
        const int32_t sx = static_cast<int32_t>((std::sin(t * 0.045f) * 0.5f + 0.5f) * static_cast<float>(kPatternW - sq));
        const int32_t sy = static_cast<int32_t>((std::cos(t * 0.033f) * 0.5f + 0.5f) * static_cast<float>(kPatternH - sq));
        uint8_t *row = dst + static_cast<size_t>(y) * kPatternW * 4;
        for (int32_t x = 0; x < static_cast<int32_t>(kPatternW); x++) {
            uint8_t pr = static_cast<uint8_t>(std::min(std::max(r, 0), 255));
            uint8_t pg = static_cast<uint8_t>(std::min(std::max(g, 0), 255));
            uint8_t pb = static_cast<uint8_t>(std::min(std::max(b, 0), 255));
            if (x >= sx && x < sx + static_cast<int32_t>(sq) && static_cast<int32_t>(y) >= sy && static_cast<int32_t>(y) < sy + static_cast<int32_t>(sq)) {
                pr = 255;
                pg = 235;
                pb = 0;
            }
            // A8B8G8R8 guest layout is RGBA in memory (little endian),
            // matching the R8G8B8A8 upload image byte order.
            row[x * 4 + 0] = pr;
            row[x * 4 + 1] = pg;
            row[x * 4 + 2] = pb;
            row[x * 4 + 3] = 255;
        }
    }
}

std::string driver_version_string(uint32_t vendor_id, uint32_t version_raw) {
    // Mirrors renderer.cpp: NVIDIA on Windows uses a different convention.
    if (vendor_id == 4318)
        return fmt::format("{}.{}.{}.{}", (version_raw >> 22) & 0x3ff, (version_raw >> 14) & 0x0ff, (version_raw >> 6) & 0x0ff, version_raw & 0x003f);
    return fmt::format("{}.{}.{}", (version_raw >> 22) & 0x3ff, (version_raw >> 12) & 0x3ff, version_raw & 0xfff);
}

struct VulkanDemoSession {
    std::mutex lock;

    // Held from start() to stop(): the memory-model test (step 5) must not
    // run while this session owns the fault handler and the 4 GiB
    // reservation.
    std::unique_lock<std::mutex> heavy_lock;

    // The frame host is owned by the session (renderer::State::frame is a
    // non-owning pointer); it outlives the renderer state.
    std::unique_ptr<vita::ios::IOSFrameHost> frame_host;
    std::unique_ptr<renderer::State> renderer_state;
    std::unique_ptr<MemState> mem;
    Address frame_block = 0;
    std::unique_ptr<DisplayState> display;
    std::unique_ptr<GxmState> gxm;
    std::unique_ptr<std::thread> thread;

    std::atomic<bool> abort_requested{ false };
    std::atomic<uint64_t> frames_presented{ 0 };
    std::atomic<uint64_t> frame_count{ 0 };
    std::chrono::steady_clock::time_point start_time;

    std::string start_report; // filled at start, shown with the live report
    bool running = false;
};

VulkanDemoSession &session() {
    static VulkanDemoSession s;
    return s;
}

// Main-thread -> render-session handoff for the UIKit-owned CAMetalLayer.
// Swift delivers it on the main thread (vita3k_ios_frame_host_set_layer,
// implemented in frame_host_ios.mm) before starting the session; start()
// exchanges it out of the shared frame-host channel here. Keeping the
// pointer out of start()'s arguments means the render thread's entry point
// only ever sees value-typed state (paths, sizes).

bool write_report(char *out, size_t out_size, const std::string &report) {
    if (!out || out_size == 0 || report.size() + 1 > out_size)
        return false;
    std::snprintf(out, out_size, "%s", report.c_str());
    return true;
}

std::string list_extensions(const std::vector<vk::ExtensionProperties> &extensions, size_t limit = 40) {
    std::string out;
    for (size_t i = 0; i < extensions.size() && i < limit; i++) {
        if (i)
            out += ", ";
        out += extensions[i].extensionName.data();
    }
    if (extensions.size() > limit)
        out += fmt::format(" (+{} more)", extensions.size() - limit);
    return out;
}

std::string describe_gpu(const renderer::vulkan::VKState &vk) {
    const auto &props = vk.physical_device_properties;
    std::string report;
    report += "gpu: " + std::string(props.deviceName.data());
    report += fmt::format(" (vendor=0x{:04x} type={})", static_cast<uint32_t>(props.vendorID), std::string(vk::to_string(props.deviceType)));
    report += fmt::format("\ndriver_version: {} (api header VK_HEADER_VERSION={})", driver_version_string(props.vendorID, props.driverVersion), VK_HEADER_VERSION);
    report += "\nsurface_format: " + std::string(vk::to_string(vk.screen_renderer.surface_format.format)) + " / " + std::string(vk::to_string(vk.screen_renderer.surface_format.colorSpace));
    report += fmt::format("\npresent_mode: {}", std::string(vk::to_string(vk.screen_renderer.present_mode)));
    const auto &caps = vk.screen_renderer.surface_capabilities;
    report += fmt::format("\nsurface_extent: {}x{} (min {}x{}, max {}x{}, imageCount {}-{})",
        caps.currentExtent.width, caps.currentExtent.height, caps.minImageExtent.width, caps.minImageExtent.height,
        caps.maxImageExtent.width, caps.maxImageExtent.height, caps.minImageCount,
        caps.maxImageCount ? caps.maxImageCount : caps.minImageCount);
    const auto &limits = props.limits;
    report += fmt::format("\nlimits: maxImageDimension2D={} maxColorAttachments={} maxBoundDescriptorSets={} maxPushConstants={} MB",
        limits.maxImageDimension2D, limits.maxColorAttachments, limits.maxBoundDescriptorSets, limits.maxPushConstantsSize / 1024);
    report += "\ndeep_stencil: " + std::string(vk::to_string(vk.deep_stencil_use));
    report += "\nmemory_mapping: disabled (Apple — vertex buffers are copied)";
    report += fmt::format("\nmemory_heaps: {} (", vk.physical_device_memory.memoryHeaps.size());
    for (size_t i = 0; i < vk.physical_device_memory.memoryHeaps.size(); i++) {
        if (i)
            report += ", ";
        report += fmt::format("{} = {} MiB", i, vk.physical_device_memory.memoryHeaps[i].size / (1024 * 1024));
    }
    report += ")";
    return report;
}

void render_loop(VulkanDemoSession &s) {
    renderer::State &state = *s.renderer_state;
    DisplayState &display = *s.display;
    GxmState &gxm = *s.gxm;
    MemState &mem = *s.mem;
    auto *gate = s.frame_host->render_gate();
    renderer::RenderGate::Binding binding(gate, [&state] { state.wait_idle(); });

    auto *const guest = mem.memory.get() + s.frame_block;
    std::chrono::steady_clock::time_point next = std::chrono::steady_clock::now();

    while (!s.abort_requested.load(std::memory_order_relaxed)) {
        auto access = gate->acquire(s.abort_requested);
        if (s.abort_requested.load(std::memory_order_relaxed))
            break;
        const uint64_t frame = s.frame_count.fetch_add(1, std::memory_order_relaxed);
        write_test_pattern(guest, static_cast<uint32_t>(frame));

        DisplayFrameInfo info;
        info.base = Ptr<const void>(s.frame_block);
        info.pitch = kPatternPitch;
        info.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
        info.image_size = { static_cast<int32_t>(kPatternW), static_cast<int32_t>(kPatternH) };
        {
            std::lock_guard<std::mutex> guard(display.display_info_mutex);
            display.next_rendered_frame = info;
        }

        state.should_display = true;
        state.render_frame(display, gxm, mem);
        state.swap_window();
        s.frames_presented.fetch_add(1, std::memory_order_relaxed);

        // Pace the loop: MoltenVK paces the GPU at the display refresh; the
        // sleep keeps the CPU (pattern generation) from spinning between
        // presents. Rebase if a frame overshot the budget.
        next += std::chrono::milliseconds(16);
        const auto now = std::chrono::steady_clock::now();
        if (now > next)
            next = now + std::chrono::milliseconds(16);
        std::this_thread::sleep_until(next);
    }
}

} // namespace

extern "C" {

int vita3k_ios_vulkan_demo_start(const char *base_path, const char *static_assets_path,
    int has_moltenvk, int width, int height, char *out, size_t out_size) {
    auto &s = session();

    // Non-blocking: the step 5 memory test holds this lock for its whole run
    // (on device it can poll a StikDebug attach); tell the UI to retry.
    s.heavy_lock = std::unique_lock<std::mutex>(vita::ios::heavy_test_lock(), std::try_to_lock);
    if (!s.heavy_lock.owns_lock()) {
        const std::string report = "start: the memory-model test is still running — wait for it to finish and retry";
        return write_report(out, out_size, report) ? -7 : -4;
    }

    const std::lock_guard<std::mutex> lock(s.lock);

    std::string report;
    // Every failure path releases the session-held heavy lock (the session
    // is not left running); only the success path keeps it until stop().
    auto fail = [&](int code, const std::string &detail) -> int {
        report += detail + "\n";
        s.heavy_lock.release();
        if (!write_report(out, out_size, report))
            return -4;
        return code;
    };

    if (!base_path || !static_assets_path || width <= 0 || height <= 0)
        return fail(-1, "start: invalid arguments (base_path/static_assets_path/size)");

    if (s.running)
        return fail(-6, "start: a Vulkan demo session is already running");

    if (!has_moltenvk)
        return fail(-5, "start: MoltenVK is not linked into this build (simulator flavor has no "
                        "MoltenVK slice — run the device flavor to present Vulkan)");

    // Consume the layer the main thread delivered (exchange: a start without
    // a fresh delivery fails instead of reusing a stale layer).
    void *metal_layer = vita::ios::take_pending_metal_layer();
    if (!metal_layer)
        return fail(-1, "start: CAMetalLayer not delivered — call vita3k_ios_frame_host_set_layer "
                        "on the main thread before starting the demo");

    // Root: writable state under the app sandbox (same layout as the
    // self-test); immutable assets (shaders-builtin/, icons/) from the
    // app bundle.
    Root root;
    root.set_vita_fs_path(base_path);
    root.set_patch_path(base_path);
    root.set_log_path(std::string(base_path) + "/logs");
    root.set_config_path(std::string(base_path) + "/config");
    root.set_shared_path(std::string(base_path) + "/shared");
    root.set_cache_path(std::string(base_path) + "/cache");
    root.set_static_assets_path(static_assets_path);

    if (logging::init(root, /*use_stdout=*/false) != Success) {
        // Already initialized (self-test ran first) — the existing sink is
        // fine, the demo just logs through it.
    }
    logging::set_level(spdlog::level::info);
    LOG_INFO("Vita3K iOS Vulkan demo: starting ({}x{} px, moltenvk={})", width, height, has_moltenvk);

    auto frame_host = std::make_unique<vita::ios::IOSFrameHost>(metal_layer, width, height);

    Config config;
    std::unique_ptr<renderer::State> state;
    bool renderer_ok = false;
    try {
        renderer_ok = renderer::init(*frame_host, state, renderer::Backend::Vulkan, config, root);
    } catch (const std::exception &e) {
        report += std::string("renderer: exception during init: ") + e.what() + "\n";
    }
    if (!renderer_ok) {
        state.reset();
        return fail(-2, "start: renderer::init(Vulkan) failed — see vita3k.log (instance/surface/device/swapchain creation)");
    }

    auto mem_state = std::make_unique<MemState>();
    if (!::init(*mem_state, /*use_page_table=*/false)) {
        // tear down the renderer before reporting: do not leak the instance
        state->cleanup();
        state.reset();
        return fail(-3, "start: mem::init failed (the 4 GiB guest reservation was refused)");
    }

    const Address block = ::alloc(*mem_state, kPatternW * kPatternH * 4, "vulkan_demo_frame");
    if (!block) {
        ::deinit_mem(*mem_state);
        state->cleanup();
        state.reset();
        return fail(-3, "start: ::alloc(960x544 RGBA) failed");
    }

    s.frame_host = std::move(frame_host);
    s.renderer_state = std::move(state);
    s.mem = std::move(mem_state);
    s.frame_block = block;
    s.display = std::make_unique<DisplayState>();
    s.gxm = std::make_unique<GxmState>();
    s.frames_presented = 0;
    s.frame_count = 0;
    s.abort_requested = false;
    s.start_time = std::chrono::steady_clock::now();

    // Post-init report: what the MoltenVK device actually reports on this
    // machine (the plan asks for extensions/formats/limits to be queried
    // and recorded, enabling only what is available).
    if (auto *vk = dynamic_cast<renderer::vulkan::VKState *>(s.renderer_state.get())) {
        s.start_report = describe_gpu(*vk);
        s.start_report += "\ninstance_extensions: " + list_extensions(vk::enumerateInstanceExtensionProperties());
        s.start_report += "\ndevice_extensions: " + list_extensions(vk->physical_device.enumerateDeviceExtensionProperties(), 80);
        s.start_report += fmt::format("\nswapchain: {} image(s), extent {}x{}",
            vk->screen_renderer.swapchain_images.size(), vk->screen_renderer.extent.width, vk->screen_renderer.extent.height);
        s.start_report += "\ntest_pattern: 960x544 RGBA in guest memory, presented via the production screen path";
    }

    try {
        s.thread = std::make_unique<std::thread>(render_loop, std::ref(s));
    } catch (const std::system_error &e) {
        // Full teardown: the render loop never started.
        s.renderer_state->cleanup();
        s.renderer_state.reset();
        ::free(*s.mem, s.frame_block);
        s.frame_block = 0;
        ::deinit_mem(*s.mem);
        s.mem.reset();
        s.display.reset();
        s.gxm.reset();
        s.frame_host.reset();
        return fail(-2, "start: render thread failed to start: " + std::string(e.what()));
    }
    s.running = true;
    vita::ios::register_active_frame_host(s.frame_host.get());

    report = s.start_report;
    report += "\nresult: demo running (frames presented are reported by vita3k_ios_vulkan_demo_report)";
    if (!write_report(out, out_size, report))
        return -4;
    return 0;
}

int vita3k_ios_vulkan_demo_stop(void) {
    auto &s = session();
    std::lock_guard<std::mutex> lock(s.lock);
    if (!s.running)
        return -1;

    s.abort_requested = true;
    if (s.thread && s.thread->joinable())
        s.thread->join();
    s.thread.reset();

    // Teardown order: render thread gone -> waitIdle + destroy
    // swapchain/surface/device/instance -> free the guest block -> deinit the
    // reservation. The dispatcher (defaultDispatchLoaderDynamic) is left
    // initialized; a later session re-initializes it from the same linked
    // MoltenVK.
    if (s.renderer_state) {
        s.renderer_state->cleanup();
        s.renderer_state.reset();
    }
    if (s.mem) {
        if (s.frame_block)
            ::free(*s.mem, s.frame_block);
        s.frame_block = 0;
        ::deinit_mem(*s.mem);
        s.mem.reset();
    }
    s.display.reset();
    s.gxm.reset();
    if (s.frame_host)
        vita::ios::unregister_active_frame_host(s.frame_host.get());
    s.frame_host.reset();
    s.running = false;
    // Release the session-held lock so the memory-model test can run again.
    s.heavy_lock.release();

    LOG_INFO("Vita3K iOS Vulkan demo: stopped ({} frames presented)", s.frames_presented.load());
    return 0;
}

int vita3k_ios_vulkan_demo_is_running(void) {
    auto &s = session();
    return s.running ? 1 : 0;
}

int vita3k_ios_vulkan_demo_report(char *out, size_t out_size) {
    auto &s = session();
    std::lock_guard<std::mutex> lock(s.lock);

    std::string report;
    if (!s.running && s.start_report.empty())
        return !write_report(out, out_size, "vulkan demo: not started") ? -4 : 0;

    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - s.start_time).count();
    const uint64_t frames = s.frames_presented.load();
    report += fmt::format("running: {}\nframes: {}\nfps: {:.1f}\n", s.running ? "yes" : "no (stopped)", frames, elapsed > 0 ? frames / elapsed : 0.0);
    if (auto *vk = dynamic_cast<renderer::vulkan::VKState *>(s.renderer_state ? s.renderer_state.get() : nullptr)) {
        report += fmt::format("extent: {}x{}\nswapchain_images: {}\n", vk->screen_renderer.extent.width, vk->screen_renderer.extent.height, vk->screen_renderer.swapchain_images.size());
    }
    report += s.start_report;
    return write_report(out, out_size, report) ? 0 : -4;
}

} // extern "C"
