// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

namespace jit_config {

// Shared by config defaults, iOS preflight and the memory self-test. The
// StikDebug arena must be reserved before detach; guest RAM is separate.
inline constexpr int default_arena_mib = 512;
inline constexpr size_t default_arena_bytes = size_t{default_arena_mib} * 1024 * 1024;

} // namespace jit_config
