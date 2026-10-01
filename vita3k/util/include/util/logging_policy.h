// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <util/diagnostics.h>
#include <spdlog/common.h>

namespace logging {

// Diagnostics own their verbosity for the entire process. In particular,
// performance captures must not inherit a title's expensive Trace setting.
constexpr spdlog::level::level_enum ios_level(int requested, diagnostics::Mode mode) {
    if (mode == diagnostics::Mode::Performance)
        return spdlog::level::info;
    if (mode == diagnostics::Mode::Compat || mode == diagnostics::Mode::Graphics)
        return spdlog::level::debug;
    return requested >= spdlog::level::trace && requested <= spdlog::level::off
        ? static_cast<spdlog::level::level_enum>(requested) : spdlog::level::off;
}

} // namespace logging
