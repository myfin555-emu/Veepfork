// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#include <ios/ime.h>
#include <dialog/state.h>
#include <ime/state.h>

#include <algorithm>
#include <cstring>

namespace vita::ios {
namespace {
// Lock order matches common-dialog initialization and the renderer overlay.
int kind(const Ime &ime, const DialogState &dialog) {
    if (dialog.type == IME_DIALOG && dialog.status == SCE_COMMON_DIALOG_STATUS_RUNNING)
        return 2;
    return ime.state ? 1 : 0;
}

uint32_t limit(const Ime &ime, const DialogState &dialog) {
    return std::min<uint32_t>(kind(ime, dialog) == 2 ? dialog.ime.max_length : ime.param.maxTextLength, SCE_IME_MAX_TEXT_LENGTH);
}

bool high(char16_t c) { return c >= 0xD800 && c <= 0xDBFF; }
bool low(char16_t c) { return c >= 0xDC00 && c <= 0xDFFF; }

bool valid_utf16(std::u16string_view text) {
    for (size_t i = 0; i < text.size(); ++i) {
        if (high(text[i])) {
            if (++i == text.size() || !low(text[i])) return false;
        } else if (low(text[i]) || text[i] == 0) {
            return false;
        }
    }
    return true;
}
}

int ime_snapshot(Ime &ime, DialogState &dialog, Vita3KImeSnapshot &out) {
    std::lock_guard dlock(dialog.mutex);
    std::lock_guard ilock(ime.mutex);
    out = {};
    out.kind = kind(ime, dialog);
    if (!out.kind) return 0;
    out.request_id = ime.request_id;
    out.max_length = limit(ime, dialog);
    out.text_length = std::min<size_t>(ime.str.size(), out.max_length);
    if (out.text_length && high(ime.str[out.text_length - 1])) --out.text_length;
    std::memcpy(out.text, ime.str.data(), out.text_length * sizeof(uint16_t));
    out.caret = std::min(ime.caretIndex, out.text_length);
    out.keyboard_type = ime.param.type;
    out.multiline = (ime.param.option & SCE_IME_OPTION_MULTILINE) != 0;
    out.cancelable = out.kind == 1 || dialog.ime.cancelable;
    if (out.kind == 2) {
        std::memcpy(out.title, dialog.ime.title, sizeof(out.title) - 1);
    }
    return out.kind;
}

int ime_replace(Ime &ime, DialogState &dialog, uint64_t request_id, std::u16string_view text, uint32_t caret) {
    std::lock_guard dlock(dialog.mutex);
    std::lock_guard ilock(ime.mutex);
    if (!kind(ime, dialog) || ime.request_id != request_id) return -1;
    if (text.size() > limit(ime, dialog) || caret > text.size() || !valid_utf16(text)) return -2;
    if (caret && caret < text.size() && high(text[caret - 1]) && low(text[caret])) --caret;
    const bool changed = ime.str != text;
    const bool moved = ime.caretIndex != caret;
    if (!changed && !moved) return 0;
    ime.str = text;
    ime.caretIndex = ime.edit_text.caretIndex = caret;
    ime.edit_text.editIndex = 0;
    ime.edit_text.preeditIndex = caret;
    ime.edit_text.preeditLength = 0;
    ime.edit_text.editLengthChange = 0;
    // A caret notification must not overwrite a text update the game has
    // not consumed yet (UIKit can deliver both for a single edit).
    if (changed || ime.event_id != SCE_IME_EVENT_UPDATE_TEXT)
        ime.event_id = changed ? SCE_IME_EVENT_UPDATE_TEXT : SCE_IME_EVENT_UPDATE_CARET;
    return 0;
}

int ime_finish(Ime &ime, DialogState &dialog, uint64_t request_id, bool cancel) {
    std::lock_guard dlock(dialog.mutex);
    std::lock_guard ilock(ime.mutex);
    const int active = kind(ime, dialog);
    if (!active || ime.request_id != request_id) return -1;
    if (active == 2) {
        if (cancel && !dialog.ime.cancelable) return -2;
        if (!cancel) {
            if (!dialog.ime.result) return -2;
            auto count = std::min<size_t>(ime.str.size(), limit(ime, dialog));
            if (count && high(ime.str[count - 1])) --count;
            std::memcpy(dialog.ime.result, ime.str.data(), count * sizeof(uint16_t));
            dialog.ime.result[count] = 0;
        }
        dialog.ime.status = cancel ? SCE_IME_DIALOG_BUTTON_CLOSE : SCE_IME_DIALOG_BUTTON_ENTER;
        dialog.result = cancel ? SCE_COMMON_DIALOG_RESULT_USER_CANCELED : SCE_COMMON_DIALOG_RESULT_OK;
        dialog.status = SCE_COMMON_DIALOG_STATUS_FINISHED;
    }
    ime.event_id = cancel ? SCE_IME_EVENT_PRESS_CLOSE : SCE_IME_EVENT_PRESS_ENTER;
    return 0;
}
}
