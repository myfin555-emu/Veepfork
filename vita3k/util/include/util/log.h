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

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include <boost/core/demangle.hpp>
#include <boost/describe/enum.hpp>
#include <boost/describe/enum_to_string.hpp>
#include <spdlog/spdlog.h>
#include <util/exit_code.h>
#include <util/fs.h>

#include <atomic>
#include <functional>
#include <type_traits>

#if defined(__APPLE__) && TARGET_OS_IOS
// spdlog filters after argument evaluation. Avoid formatting strings and
// reading guest memory for diagnostic arguments when the log is disabled.
#define LOG_AT_LEVEL(level, ...) (spdlog::should_log(level) ? SPDLOG_LOGGER_CALL(spdlog::default_logger_raw(), level, __VA_ARGS__) : static_cast<void>(0))
#define LOG_TRACE(...) LOG_AT_LEVEL(spdlog::level::trace, __VA_ARGS__)
#define LOG_DEBUG(...) LOG_AT_LEVEL(spdlog::level::debug, __VA_ARGS__)
#define LOG_INFO(...) LOG_AT_LEVEL(spdlog::level::info, __VA_ARGS__)
#define LOG_WARN(...) LOG_AT_LEVEL(spdlog::level::warn, __VA_ARGS__)
#define LOG_ERROR(...) LOG_AT_LEVEL(spdlog::level::err, __VA_ARGS__)
#define LOG_CRITICAL(...) LOG_AT_LEVEL(spdlog::level::critical, __VA_ARGS__)
#else
#define LOG_TRACE SPDLOG_TRACE
#define LOG_DEBUG SPDLOG_DEBUG
#define LOG_INFO SPDLOG_INFO
#define LOG_WARN SPDLOG_WARN
#define LOG_ERROR SPDLOG_ERROR
#define LOG_CRITICAL SPDLOG_CRITICAL
#endif

#define LOG_IF(log_function, flag, ...) ((flag) ? log_function(__VA_ARGS__) : static_cast<void>(0))

#define LOG_TRACE_IF(flag, ...) LOG_IF(LOG_TRACE, flag, __VA_ARGS__)
#define LOG_DEBUG_IF(flag, ...) LOG_IF(LOG_DEBUG, flag, __VA_ARGS__)
#define LOG_INFO_IF(flag, ...) LOG_IF(LOG_INFO, flag, __VA_ARGS__)
#define LOG_WARN_IF(flag, ...) LOG_IF(LOG_WARN, flag, __VA_ARGS__)
#define LOG_ERROR_IF(flag, ...) LOG_IF(LOG_ERROR, flag, __VA_ARGS__)
#define LOG_CRITICAL_IF(flag, ...) LOG_IF(LOG_CRITICAL, flag, __VA_ARGS__)

#define LOG_ONCE(log_function, ...)         \
    do {                                    \
        static std::atomic_flag has_logged; \
        if (!has_logged.test_and_set())     \
            log_function(__VA_ARGS__);      \
    } while (0)

#if defined(__APPLE__) && TARGET_OS_IOS
// Do not consume the once flag (or perform its atomic RMW) while filtered out.
#define LOG_ONCE_AT_LEVEL(level, log_function, ...) \
    do { \
        if (spdlog::should_log(level)) \
            LOG_ONCE(log_function, __VA_ARGS__); \
    } while (0)
#define LOG_TRACE_ONCE(...) LOG_ONCE_AT_LEVEL(spdlog::level::trace, LOG_TRACE, __VA_ARGS__)
#define LOG_DEBUG_ONCE(...) LOG_ONCE_AT_LEVEL(spdlog::level::debug, LOG_DEBUG, __VA_ARGS__)
#define LOG_INFO_ONCE(...) LOG_ONCE_AT_LEVEL(spdlog::level::info, LOG_INFO, __VA_ARGS__)
#define LOG_WARN_ONCE(...) LOG_ONCE_AT_LEVEL(spdlog::level::warn, LOG_WARN, __VA_ARGS__)
#define LOG_ERROR_ONCE(...) LOG_ONCE_AT_LEVEL(spdlog::level::err, LOG_ERROR, __VA_ARGS__)
#define LOG_CRITICAL_ONCE(...) LOG_ONCE_AT_LEVEL(spdlog::level::critical, LOG_CRITICAL, __VA_ARGS__)
#else
#define LOG_TRACE_ONCE(...) LOG_ONCE(LOG_TRACE, __VA_ARGS__)
#define LOG_DEBUG_ONCE(...) LOG_ONCE(LOG_DEBUG, __VA_ARGS__)
#define LOG_INFO_ONCE(...) LOG_ONCE(LOG_INFO, __VA_ARGS__)
#define LOG_WARN_ONCE(...) LOG_ONCE(LOG_WARN, __VA_ARGS__)
#define LOG_ERROR_ONCE(...) LOG_ONCE(LOG_ERROR, __VA_ARGS__)
#define LOG_CRITICAL_ONCE(...) LOG_ONCE(LOG_CRITICAL, __VA_ARGS__)
#endif

namespace logging {

ExitCode init(const Root &root_paths, bool use_stdout);
void set_level(spdlog::level::level_enum log_level);
bool is_enabled();
ExitCode add_sink(const fs::path &log_path);
void set_log_callback(std::function<void(std::string, int)> cb);

} // namespace logging

#define RET_ERROR(error)                                                            \
    ([&]() {                                                                        \
        LOG_ERROR_ONCE("{} returned {} ({})", export_name, #error, log_hex(error)); \
        return static_cast<int>(error);                                             \
    })()

/*
    returns: A string with the input number formatted in hexadecimal
    Examples:
        * `12` returns: `"0xC"`
        * `1337` returns: `"0x539"`
        * `72742069` returns: `"0x455F4B5"`
*/
template <typename T>
std::string log_hex(T val) {
    return fmt::format("0x{:X}", static_cast<std::make_unsigned_t<T>>(val));
}

/*
    returns: A string with the input number formatted in hexadecimal with padding of the inputted type size
    Examples:
        * `uint8_t 5` returns: `"0x05"`
        * `uint8_t 15` returns: `"0x0F"`
        * `uint8_t 255` returns: `"0xFF"`

        * `uint16_t 15` returns: `"0x000F"`
        * `uint16_t 1337` returns: `"0x0539"`
        * `uint16_t 65535` returns: `"0xFFFF"`

        * `uint32_t 15` returns: `"0x0000000F"`
        * `uint32_t 1337` returns: `"0x00000539"`
        * `uint32_t 65535` returns: `"0x0000FFFF"`
        * `uint32_t 134217728` returns: `"0x08000000"`
*/
template <typename T>
std::string log_hex_full(T val) {
    return fmt::format("0x{:0{}X}", static_cast<std::make_unsigned_t<T>>(val), sizeof(T) * 2);
}

template <class T>
class Ptr;
FMT_BEGIN_NAMESPACE
template <typename T, typename Char>
struct formatter<Ptr<T>, Char> : formatter<string_view, Char> {
public:
    template <typename FormatContext>
    auto format(const Ptr<T> p, FormatContext &ctx) const {
        return detail::write(ctx.out(),
            basic_string_view<Char>(log_hex_full(p.address())));
    }
};
template <typename T, typename Char>
    requires(boost::describe::has_describe_enumerators<T>::value)
struct formatter<T, Char> : formatter<string_view, Char> {
public:
    template <typename FormatContext>
    auto format(const T e, FormatContext &ctx) const {
        auto name = boost::describe::enum_to_string(e, nullptr);
        if (name != nullptr) {
            return detail::write(ctx.out(), name);
        } else {
            auto enum_as_uint = static_cast<std::make_unsigned_t<T>>(e);
            auto enum_as_string = fmt::format("{}(0x{:0{}X})", boost::core::demangle(typeid(T).name()), enum_as_uint, sizeof(enum_as_uint) * 2);
            return detail::write(ctx.out(), basic_string_view<Char>(enum_as_string));
        }
    }
};
FMT_END_NAMESPACE
