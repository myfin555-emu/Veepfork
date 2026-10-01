// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace diagnostics {
enum class Mode { Off, Compat, Performance, Graphics };
enum class Metric { GuestFrame, HostFrame, Batch, Render, Present, Pipeline, Count };

extern std::atomic<Mode> active_mode;
inline Mode mode() { return active_mode.load(std::memory_order_relaxed); }
inline bool enabled() { return mode() != Mode::Off; }
const char *mode_name();

// Read once per process, before logging::init. Never changes saved game config.
void initialize(const std::string &base, const std::string &version);
std::string capture_path();
void start_sampler(std::function<std::string()> snapshot);
void stop_sampler();
void begin_game(const std::string &title_id);
void event(std::string_view kind, std::string_view detail);
void stage(std::string_view name);
void count(Metric metric, uint64_t elapsed_us = 0);
void guest_frame();
void host_frame();
void render_stage(unsigned stage); // 0 stopped, 1 batch, 2 render, 3 present

// Only timestamps when enabled; accumulated once per second, never per-frame I/O.
class Scope {
    Metric metric;
    bool active;
    std::chrono::steady_clock::time_point start;
public:
    explicit Scope(Metric metric) : metric(metric), active(enabled()) {
        if (active) start = std::chrono::steady_clock::now();
    }
    ~Scope() {
        if (active) count(metric, std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
    }
};
} // namespace diagnostics
