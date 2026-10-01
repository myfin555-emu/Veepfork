// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <ios/bridge.h>
#include <string_view>

struct Ime;
struct DialogState;

namespace vita::ios {
int ime_snapshot(Ime &ime, DialogState &dialog, Vita3KImeSnapshot &out);
int ime_replace(Ime &ime, DialogState &dialog, uint64_t request_id, std::u16string_view text, uint32_t caret);
int ime_finish(Ime &ime, DialogState &dialog, uint64_t request_id, bool cancel);
}
