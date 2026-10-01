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

#pragma once

#include <atomic>
#include <string>
#include <vector>

#include <renderer/frame_host.h>
#include <renderer/render_gate.h>

namespace vita::ios {

/// iOS surface provider (renderer::FrameHost implementation).
///
/// Step 6 of the iOS port: the UIKit layer of the app owns a CAMetalLayer
/// (opaque void* here) and feeds the pixel drawable size; the renderer
/// thread reads both to create the VK_EXT_metal_surface and to rebuild the
/// swapchain on resize. The main thread writes, the render thread reads,
/// so the state is lock-free.
class IOSFrameHost final : public renderer::FrameHost {
public:
    /// `metal_layer` is a `CAMetalLayer *` owned by the app (opaque here).
    IOSFrameHost(void *metal_layer = nullptr, int drawable_width = 0, int drawable_height = 0);

    void set_metal_layer(void *metal_layer);
    void set_drawable_size(int width, int height);

    renderer::DisplayHandle handle() const override;
    int drawable_width() const override;
    int drawable_height() const override;
    std::vector<std::string> font_dirs() const override;

    void *get_proc_address(const char *name) const override;
    renderer::RenderGate *render_gate() override { return &render_gate_; }

private:
    renderer::RenderGate render_gate_;
    std::atomic<void *> metal_layer_ = nullptr;
    std::atomic<int> drawable_width_ = 0;
    std::atomic<int> drawable_height_ = 0;
};

// Main-thread (UIKit) -> render-session handoff channel for the app-owned
// CAMetalLayer (implemented in frame_host_ios.mm). The app delivers the
// layer on the main thread (vita3k_ios_frame_host_set_layer in bridge.h);
// the next render session (step 6 Vulkan demo or step 7 game session)
// consumes it exactly once when it starts.
void set_pending_metal_layer(void *metal_layer);
void *take_pending_metal_layer();

// Active frame host registry: sessions register their frame host for the
// lifetime of their run so main-thread size updates (rotation/resize) reach
// whichever session is presenting.
void register_active_frame_host(IOSFrameHost *frame_host);
void unregister_active_frame_host(IOSFrameHost *frame_host);
void set_rendering_suspended(bool suspended);

} // namespace vita::ios
