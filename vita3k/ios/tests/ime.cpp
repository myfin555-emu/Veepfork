// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
// Standalone host regression: the same helpers called by the iOS C bridge.
#include <ios/ime.h>
#include <dialog/state.h>
#include <ime/state.h>
#include <array>
#include <cassert>
#include <iostream>

int main() {
    Ime ime{};
    DialogState dialog{};
    Vita3KImeSnapshot snapshot{};
    std::array<uint16_t, 17> result;
    result.fill(0xCAFE);
    assert(vita::ios::ime_snapshot(ime, dialog, snapshot) == 0);

    // sceImeDialogInit does not set Ime.state. This was invisible on iOS.
    dialog.type = IME_DIALOG;
    dialog.status = SCE_COMMON_DIALOG_STATUS_RUNNING;
    dialog.ime.max_length = 8;
    dialog.ime.result = result.data();
    dialog.ime.cancelable = false;
    ime.request_id = 1;
    ime.param.maxTextLength = 8;
    ime.str = u"Sinner";
    ime.caretIndex = 6;
    assert(!ime.state);
    assert(vita::ios::ime_snapshot(ime, dialog, snapshot) == 2);
    assert(snapshot.text_length == 6 && snapshot.max_length == 8 && snapshot.caret == 6);
    assert(snapshot.request_id == 1 && !snapshot.cancelable);
    assert(vita::ios::ime_finish(ime, dialog, 1, true) == -2);
    assert(dialog.status == SCE_COMMON_DIALOG_STATUS_RUNNING);

    // Full-text replacement handles selection, deletion and accents, with
    // the exact UTF-16 result and a terminator inside the supplied buffer.
    assert(vita::ios::ime_replace(ime, dialog, 1, u"José🙂", 6) == 0);
    assert(ime.event_id == SCE_IME_EVENT_UPDATE_TEXT);
    assert(vita::ios::ime_replace(ime, dialog, 1, u"José🙂", 2) == 0);
    assert(ime.event_id == SCE_IME_EVENT_UPDATE_TEXT); // pending text survives caret move
    assert(vita::ios::ime_replace(ime, dialog, 1, u"123456789", 9) == -2);
    const std::u16string broken(1, char16_t(0xD800));
    assert(vita::ios::ime_replace(ime, dialog, 1, broken, 1) == -2);
    assert(vita::ios::ime_finish(ime, dialog, 1, false) == 0);
    assert(dialog.status == SCE_COMMON_DIALOG_STATUS_FINISHED);
    assert(dialog.result == SCE_COMMON_DIALOG_RESULT_OK);
    assert(dialog.ime.status == SCE_IME_DIALOG_BUTTON_ENTER);
    assert(std::u16string(reinterpret_cast<char16_t *>(result.data())) == u"José🙂");
    assert(result[7] == 0xCAFE);
    assert(vita::ios::ime_snapshot(ime, dialog, snapshot) == 0);

    // A new dialog may open before the next UI poll. Old actions are stale.
    dialog.status = SCE_COMMON_DIALOG_STATUS_RUNNING;
    dialog.ime.cancelable = true;
    ++ime.request_id;
    result.fill(0xCAFE);
    assert(vita::ios::ime_replace(ime, dialog, 1, u"old", 3) == -1);
    assert(vita::ios::ime_finish(ime, dialog, 1, false) == -1);
    assert(vita::ios::ime_replace(ime, dialog, 2, u"", 0) == 0);
    assert(ime.str.empty());
    assert(vita::ios::ime_finish(ime, dialog, 2, true) == 0);
    assert(dialog.result == SCE_COMMON_DIALOG_RESULT_USER_CANCELED);
    assert(dialog.ime.status == SCE_IME_DIALOG_BUTTON_CLOSE);
    assert(result[0] == 0xCAFE); // cancel must not overwrite guest memory

    // Low-level IME uses events, not common-dialog result/status.
    dialog.type = NO_DIALOG;
    ime.state = true;
    ++ime.request_id;
    assert(vita::ios::ime_snapshot(ime, dialog, snapshot) == 1);
    assert(vita::ios::ime_replace(ime, dialog, 3, u"Player", 3) == 0);
    assert(vita::ios::ime_finish(ime, dialog, 3, false) == 0);
    assert(ime.event_id == SCE_IME_EVENT_PRESS_ENTER && ime.state);
    assert(vita::ios::ime_finish(ime, dialog, 3, true) == 0);
    assert(ime.event_id == SCE_IME_EVENT_PRESS_CLOSE);
    ime.deinit();
    assert(ime.request_id == 3); // do not reuse IDs on a runtime restart
    assert(vita::ios::ime_snapshot(ime, dialog, snapshot) == 0);
    std::cout << "Native IME: dialog detection, UTF-16, limits, results, cancel, stale requests and low-level events passed\n";
}
