// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>

namespace vita::ios {
// Runs only on an already prepared arena, with no game session active.
int run_jit_regression_tests(std::string& report);
}
