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

#include "ios/bridge.h"
#include "ios/frame_host.h"

#include <TargetConditionals.h>
#if defined(__APPLE__) && TARGET_OS_IPHONE
#include <Metal/Metal.h>
#include <QuartzCore/QuartzCore.h>
#endif

#include <algorithm>
#include <mutex>
#include <vector>

namespace vita::ios {

// Main-thread (UIKit) -> render-session handoff channel.
//
// The CAMetalLayer is owned by the app and crosses into the core as an
// opaque pointer on the main thread, where the app delivers it with
// set_pending_metal_layer() before starting a render session (the step 6
// Vulkan demo or the step 7 game session). The session then exchanges it
// out of the channel at start (a start without a fresh delivery fails
// instead of reusing a stale layer). Keeping the pointer out of the
// session start's arguments means the render thread's entry point only
// ever sees value-typed state (paths, sizes) — no UIKit object or pointer
// crosses into it.
namespace {

std::atomic<void *> g_pending_metal_layer{ nullptr };

std::mutex active_frame_hosts_mutex;
std::vector<IOSFrameHost *> active_frame_hosts;
bool rendering_suspended = false;

} // namespace

void set_pending_metal_layer(void *metal_layer) {
    g_pending_metal_layer.store(metal_layer, std::memory_order_release);
}

void *take_pending_metal_layer() {
    return g_pending_metal_layer.exchange(nullptr, std::memory_order_acq_rel);
}

// Frame hosts that must receive main-thread size updates (the render thread
// reads them lock-free and rebuilds the swapchain on the next frame). Both
// the Vulkan demo session and the game session register theirs for the
// lifetime of their run.
void register_active_frame_host(IOSFrameHost *frame_host) {
    std::lock_guard<std::mutex> guard(active_frame_hosts_mutex);
    frame_host->render_gate()->set_suspended(rendering_suspended);
    const auto it = std::find(active_frame_hosts.begin(), active_frame_hosts.end(), frame_host);
    if (it == active_frame_hosts.end())
        active_frame_hosts.push_back(frame_host);
}

void unregister_active_frame_host(IOSFrameHost *frame_host) {
    std::lock_guard<std::mutex> guard(active_frame_hosts_mutex);
    const auto it = std::find(active_frame_hosts.begin(), active_frame_hosts.end(), frame_host);
    if (it != active_frame_hosts.end())
        active_frame_hosts.erase(it);
}

void set_rendering_suspended(bool suspended) {
    std::lock_guard<std::mutex> guard(active_frame_hosts_mutex);
    rendering_suspended = suspended;
    for (auto *frame_host : active_frame_hosts)
        frame_host->render_gate()->set_suspended(suspended);
}

void notify_active_frame_host_size(const int width, const int height) {
    std::lock_guard<std::mutex> guard(active_frame_hosts_mutex);
    for (auto *frame_host : active_frame_hosts)
        frame_host->set_drawable_size(width, height);
}

} // namespace vita::ios

namespace vita::ios {

IOSFrameHost::IOSFrameHost(void *metal_layer, int drawable_width, int drawable_height)
    : metal_layer_(metal_layer), drawable_width_(drawable_width), drawable_height_(drawable_height) {
}

void IOSFrameHost::set_metal_layer(void *metal_layer) {
    metal_layer_ = metal_layer;
}

void IOSFrameHost::set_drawable_size(int width, int height) {
    drawable_width_ = width;
    drawable_height_ = height;
}

renderer::DisplayHandle IOSFrameHost::handle() const {
    return renderer::IOSDisplayHandle{
        .metal_layer = metal_layer_,
    };
}

int IOSFrameHost::drawable_width() const {
    return drawable_width_;
}

int IOSFrameHost::drawable_height() const {
    return drawable_height_;
}

std::vector<std::string> IOSFrameHost::font_dirs() const {
    return {};
}

void *IOSFrameHost::get_proc_address(const char *name) const {
    // Step 6 points this at the MoltenVK instance linked by the app; until
    // then the Vulkan symbols resolve from the app image at load time.
    return nullptr;
}

} // namespace vita::ios

extern "C" {

// Main-thread (UIKit) callbacks: the app's CAMetalLayer and its current
// pixel drawable size cross into the core through the frame-host channel.
// Neither argument is a UIKit object: the layer is an opaque pointer stored
// in lock-free state, the size is a plain pair of ints.
void vita3k_ios_frame_host_set_layer(void *metal_layer) {
    vita::ios::set_pending_metal_layer(metal_layer);
}

void vita3k_ios_frame_host_set_size(int width, int height) {
    vita::ios::notify_active_frame_host_size(width, height);
}

} // extern "C"
