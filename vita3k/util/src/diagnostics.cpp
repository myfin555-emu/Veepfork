// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later
#include <util/diagnostics.h>

#include <spdlog/async.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <array>
#include <algorithm>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

namespace diagnostics {
std::atomic<Mode> active_mode{ Mode::Off };
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::array<uint64_t, 14> bounds{ 1000, 2000, 4000, 8000, 12000, 16667, 20000, 25000, 33334, 50000, 100000, 250000, 500000, 1000000 };
struct Samples {
    uint64_t count = 0, sum = 0, max = 0;
    std::array<uint64_t, bounds.size() + 1> histogram{};
};
struct Capture {
    std::mutex mutex;
    std::mutex metrics_mutex; // never held during file I/O or platform observation
    std::condition_variable wake;
    bool stop = false;
    std::thread worker;
    std::shared_ptr<spdlog::logger> log;
    std::string path, title, phase = "startup";
    unsigned game = 0;
    Clock::time_point origin = Clock::now(), guest_last{}, host_last{}, sample_last = origin;
    std::array<Samples, static_cast<size_t>(Metric::Count)> samples{};
    ~Capture() {
        { std::lock_guard lock(mutex); stop = true; }
        wake.notify_all();
        if (worker.joinable()) worker.join();
    }
};
Capture &capture() { static Capture c; return c; }
std::atomic<unsigned> renderer_stage{ 0 };

// JSON strings may contain UTF-8, newlines, quotes, and arbitrary game titles.
std::string json_string(std::string_view value) {
    std::string out = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c < 0x20) out += fmt::format("\\u{:04x}", c);
        else out += c;
    }
    return out + '"';
}
uint64_t elapsed(Clock::time_point start, Clock::time_point now) {
    return std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();
}
void write(Capture &c, std::string_view kind, std::string_view detail) {
    if (!c.log) return;
    const auto row = fmt::format("{{\"schema\":1,\"elapsed_us\":{},\"game\":{},\"title_id\":{},\"event\":{},\"detail\":{}}}",
        elapsed(c.origin, Clock::now()), c.game, json_string(c.title), json_string(kind), json_string(detail));
    c.log->info("{}", row);
    // Keep launch context even after a long play session rotates the telemetry.
    if (kind == "capture_start" || kind == "host" || kind == "game_start" || kind == "effective_config" || kind == "game_info") {
        std::ofstream context(c.path + "/context-" + std::to_string(c.game) + ".jsonl", std::ios::app);
        context << row << '\n';
    }
}
void add(Samples &s, uint64_t us) {
    ++s.count;
    s.sum += us;
    s.max = std::max(s.max, us);
    ++s.histogram[std::lower_bound(bounds.begin(), bounds.end(), us) - bounds.begin()];
}
void frame(Metric metric, Clock::time_point &last) {
    auto &c = capture();
    std::lock_guard lock(c.metrics_mutex);
    const auto now = Clock::now();
    // The first frame has no preceding interval.
    if (last != Clock::time_point{}) add(c.samples[static_cast<size_t>(metric)], elapsed(last, now));
    last = now;
}
} // namespace

const char *mode_name() {
    switch (mode()) {
    case Mode::Compat: return "compat";
    case Mode::Performance: return "performance";
    case Mode::Graphics: return "graphics";
    default: return "off";
    }
}

void initialize(const std::string &base, const std::string &version) {
    auto &c = capture();
    if (c.log) return;
    std::ifstream marker(std::filesystem::path(base) / "diagnostics-mode.txt");
    std::string value;
    std::getline(marker, value);
    if (!value.empty() && value.back() == '\r') value.pop_back();
    Mode selected = Mode::Off;
    if (value == "compat") selected = Mode::Compat;
    else if (value == "performance") selected = Mode::Performance;
    else if (value == "graphics") selected = Mode::Graphics;
    const auto root = std::filesystem::path(base) / "diagnostics";
    std::filesystem::create_directories(root);
    std::ofstream status(root / "active.txt", std::ios::trunc);
    if (selected == Mode::Off) {
        status << "schema=1\nmode=off\n";
        if (!value.empty() && value != "off") status << "error=invalid diagnostics-mode.txt\n";
        return;
    }
    const auto epoch = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    c.path = (root / ("capture-" + std::to_string(epoch))).string();
    std::filesystem::create_directories(c.path);
    auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(c.path + "/telemetry.jsonl", 2 * 1024 * 1024, 2);
    c.log = std::make_shared<spdlog::logger>("diagnostic-telemetry", sink);
    c.log->set_pattern("%v");
    c.log->flush_on(spdlog::level::info); // independent of the asynchronous runtime log
    c.origin = c.sample_last = Clock::now();
    active_mode.store(selected);
    status << "schema=1\nmode=" << mode_name() << "\ncapture=" << std::filesystem::path(c.path).filename().string() << '\n';
    std::ofstream manifest(c.path + "/manifest.txt");
    manifest << "schema=1\nmode=" << mode_name() << "\nversion=" << version << "\nstarted_unix_us=" << epoch
             << "\nframe_histogram_upper_bounds_us=1000,2000,4000,8000,12000,16667,20000,25000,33334,50000,100000,250000,500000,1000000,inf"
             << "\nretention=runtime 3x8MiB; telemetry 3x2MiB; old captures retained until collected"
             << "\nlast_records_may_be_lost=true\n";
    write(c, "capture_start", mode_name());
}

std::string capture_path() { return capture().path; }

void event(std::string_view kind, std::string_view detail) {
    if (!enabled()) return;
    auto &c = capture();
    std::lock_guard lock(c.mutex);
    write(c, kind, detail);
}
void stage(std::string_view name) {
    if (!enabled()) return;
    auto &c = capture();
    std::lock_guard lock(c.mutex);
    c.phase = name;
    write(c, "stage", name);
}
void begin_game(const std::string &title_id) {
    if (!enabled()) return;
    auto &c = capture();
    std::lock_guard lock(c.mutex);
    c.title = title_id;
    ++c.game;
    {
        std::lock_guard metrics_lock(c.metrics_mutex);
        c.samples = {};
        c.guest_last = c.host_last = {};
    }
    c.sample_last = Clock::now();
    c.phase = "launch";
    renderer_stage.store(0);
    write(c, "game_start", title_id);
}
void count(Metric metric, uint64_t elapsed_us) {
    if (!enabled()) return;
    auto &c = capture();
    std::lock_guard lock(c.metrics_mutex);
    add(c.samples[static_cast<size_t>(metric)], elapsed_us);
}
void guest_frame() { if (enabled()) frame(Metric::GuestFrame, capture().guest_last); }
void host_frame() { if (enabled()) frame(Metric::HostFrame, capture().host_last); }
void render_stage(unsigned stage) { if (enabled()) renderer_stage.store(stage, std::memory_order_relaxed); }

void start_sampler(std::function<std::string()> snapshot) {
    if (!enabled()) return;
    auto &c = capture();
    if (c.worker.joinable()) return;
    c.stop = false;
    c.worker = std::thread([&c, snapshot = std::move(snapshot)] {
        std::unique_lock lock(c.mutex);
        while (!c.wake.wait_for(lock, std::chrono::seconds(1), [&c] { return c.stop; })) {
            // The platform observer must use try_lock and atomic snapshots only.
            const auto game = c.game;
            lock.unlock();
            std::string detail;
            try {
                detail = snapshot();
            } catch (const std::exception &e) {
                detail = std::string("snapshot_error=") + e.what();
            }
            const auto pool = spdlog::thread_pool();
            lock.lock();
            if (game != c.game) continue; // observer overlapped a game switch
            decltype(c.samples) samples;
            Clock::time_point guest_last, host_last, now;
            {
                std::lock_guard metrics_lock(c.metrics_mutex);
                now = Clock::now();
                samples = c.samples;
                c.samples = {};
                guest_last = c.guest_last;
                host_last = c.host_last;
            }
            const auto window_us = elapsed(c.sample_last, now);
            c.sample_last = now;
            write(c, "health", fmt::format("phase={} renderer_stage={} window_us={} guest_age_us={} host_age_us={} log_overruns={} {}",
                c.phase, renderer_stage.load(), window_us,
                guest_last == Clock::time_point{} ? -1LL : static_cast<long long>(elapsed(guest_last, now)),
                host_last == Clock::time_point{} ? -1LL : static_cast<long long>(elapsed(host_last, now)),
                pool ? pool->overrun_counter() : 0, detail));
            constexpr const char *names[] = { "guest_frame_interval", "host_frame_interval", "batch", "render", "present", "pipeline" };
            for (size_t i = 0; i < samples.size(); ++i) {
                const auto &s = samples[i];
                std::string histogram;
                for (auto n : s.histogram) histogram += (histogram.empty() ? "" : ",") + std::to_string(n);
                write(c, "metric", fmt::format("name={} window_us={} count={} sum_us={} max_us={} histogram={}", names[i], window_us, s.count, s.sum, s.max, histogram));
            }
            if (auto logger = spdlog::default_logger()) logger->flush();
        }
    });
}
void stop_sampler() {
    auto &c = capture();
    { std::lock_guard lock(c.mutex); c.stop = true; }
    c.wake.notify_all();
    if (c.worker.joinable()) c.worker.join();
}
} // namespace diagnostics
