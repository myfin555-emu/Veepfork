// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string_view>

namespace features {
inline bool use_killzone_vpck_workaround(bool enabled, std::string_view title_id, std::string_view version) {
    return enabled && title_id == "PCSA00107" && version == "1.00";
}
inline bool use_resistance_f16_workaround(bool enabled, std::string_view title_id, std::string_view version) {
    return enabled && title_id == "PCSA00063" && version == "1.00";
}
} // namespace features
