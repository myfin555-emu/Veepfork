// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <utility>

namespace renderer {

// Optional host lifecycle gate. A render iteration owns the lock until all
// its submissions/presentation are done. Suspension closes admission, waits
// for that iteration, then drains the GPU before returning to the OS.
class RenderGate {
    std::mutex mutex;
    std::condition_variable ready;
    std::atomic<bool> suspended{ false };
    std::function<void()> drain_gpu;

public:
    bool is_suspended() const { return suspended.load(std::memory_order_acquire); }

    void set_suspended(bool value) {
        suspended.store(value, std::memory_order_release);
        std::lock_guard lock(mutex);
        if (value && drain_gpu)
            drain_gpu();
        ready.notify_all();
    }

    std::unique_lock<std::mutex> acquire(const std::atomic<bool> &abort) {
        std::unique_lock lock(mutex);
        // Shutdown must also leave a suspended render loop. A short timed
        // wait makes the existing abort flag sufficient, including the demo.
        while (is_suspended() && !abort.load(std::memory_order_relaxed))
            ready.wait_for(lock, std::chrono::milliseconds(10));
        return lock;
    }

    // Keeps the drain callback alive exactly as long as the render thread;
    // it can never refer to a destroyed graphics device.
    class Binding {
        RenderGate *gate;

    public:
        Binding(RenderGate *gate, std::function<void()> drain)
            : gate(gate) {
            if (gate) {
                std::lock_guard lock(gate->mutex);
                gate->drain_gpu = std::move(drain);
            }
        }
        ~Binding() {
            if (gate) {
                std::lock_guard lock(gate->mutex);
                if (gate->drain_gpu)
                    gate->drain_gpu();
                gate->drain_gpu = {};
            }
        }
        Binding(const Binding &) = delete;
        Binding &operator=(const Binding &) = delete;
    };
};

} // namespace renderer
