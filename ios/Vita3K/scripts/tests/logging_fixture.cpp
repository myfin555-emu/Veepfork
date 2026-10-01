// Exercise the iOS macro branch on the macOS test host.
#include <TargetConditionals.h>
#undef TARGET_OS_IOS
#define TARGET_OS_IOS 1
#include <util/log.h>
#include <util/logging_policy.h>
#include <spdlog/sinks/ostream_sink.h>

#include <cassert>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

static void once(std::atomic_int &evaluations) {
    LOG_ERROR_ONCE("once {}", ++evaluations);
}

int main() {
    std::ostringstream output;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(output);
    auto logger = std::make_shared<spdlog::logger>("test", sink);
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("%v");
    spdlog::set_level(spdlog::level::off);
    std::atomic_int evaluations{ 0 };
    for (int i = 0; i < 100000; ++i) {
        LOG_TRACE("{}", ++evaluations);
        LOG_DEBUG("{}", ++evaluations);
        LOG_INFO("{}", ++evaluations);
        LOG_WARN("{}", ++evaluations);
        LOG_ERROR("{}", ++evaluations);
        LOG_CRITICAL("{}", ++evaluations);
        LOG_TRACE_IF(true, "{}", ++evaluations);
        once(evaluations);
    }
    assert(evaluations == 0 && output.str().empty());
    spdlog::set_level(spdlog::level::debug);
    LOG_TRACE("{}", ++evaluations);
    LOG_DEBUG_IF(false, "{}", ++evaluations);
    assert(evaluations == 0);
    LOG_DEBUG("debug {}", ++evaluations);
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i)
        threads.emplace_back([&] { once(evaluations); });
    for (auto &thread : threads)
        thread.join();
    assert(evaluations == 2);
    assert(output.str() == "debug 1\nonce 2\n");

    using diagnostics::Mode;
    for (int level = -1; level <= 7; ++level) {
        assert(logging::ios_level(level, Mode::Compat) == spdlog::level::debug);
        assert(logging::ios_level(level, Mode::Graphics) == spdlog::level::debug);
        assert(logging::ios_level(level, Mode::Performance) == spdlog::level::info);
        assert(logging::ios_level(level, Mode::Off) == (level >= 0 && level <= 6 ? level : 6));
    }
    std::cout << "800000 disabled calls: 0 argument evaluations; once enabled: exactly 1; diagnostic precedence: passed\n";
}
