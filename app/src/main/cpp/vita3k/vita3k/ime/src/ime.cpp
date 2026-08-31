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

#include <ime/functions.h>

#include <algorithm>

namespace {
bool splits_surrogate(const std::u16string &text, size_t index) {
    return index > 0 && index < text.size() && text[index - 1] >= 0xD800 && text[index - 1] <= 0xDBFF
        && text[index] >= 0xDC00 && text[index] <= 0xDFFF;
}

uint32_t bounded_index(const std::u16string &text, uint32_t index) {
    index = std::min(index, static_cast<uint32_t>(text.size()));
    return splits_surrogate(text, index) ? index - 1 : index;
}

void normalize_edit_state(Ime &ime) {
    ime.edit_text.caretIndex = bounded_index(ime.str, ime.edit_text.caretIndex);
    ime.edit_text.preeditIndex = bounded_index(ime.str, ime.edit_text.preeditIndex);
    ime.edit_text.preeditLength = std::min(ime.edit_text.preeditLength,
        static_cast<uint32_t>(ime.str.size()) - ime.edit_text.preeditIndex);
}
} // namespace

void ime_commit_text(Ime &ime, const std::u16string &text) {
    if (text.empty())
        return;
    normalize_edit_state(ime);

    if (ime.edit_text.preeditLength > 0) {
        ime.str.erase(ime.edit_text.preeditIndex, ime.edit_text.preeditLength);
        ime.edit_text.caretIndex = ime.edit_text.preeditIndex;
        ime.edit_text.preeditLength = 0;
    }

    const uint32_t remaining = ime.param.maxTextLength > ime.str.size() ? ime.param.maxTextLength - static_cast<uint32_t>(ime.str.size()) : 0;
    const uint32_t insert_len = bounded_index(text, remaining);
    if (insert_len == 0)
        return;

    const std::u16string to_insert = text.substr(0, insert_len);
    ime.edit_text.editIndex = ime.edit_text.caretIndex;
    ime.str.insert(ime.edit_text.caretIndex, to_insert);
    ime.edit_text.caretIndex += insert_len;
    ime.caretIndex = ime.edit_text.caretIndex;

    ime.edit_text.preeditIndex = ime.edit_text.caretIndex;
    ime.edit_text.preeditLength = 0;
    ime.edit_text.editLengthChange = 0;

    ime.event_id = SCE_IME_EVENT_UPDATE_TEXT;
}

void ime_set_preedit(Ime &ime, const std::u16string &preedit) {
    normalize_edit_state(ime);
    if (ime.edit_text.preeditLength > 0) {
        ime.str.erase(ime.edit_text.preeditIndex, ime.edit_text.preeditLength);
        ime.edit_text.caretIndex = ime.edit_text.preeditIndex;
        ime.edit_text.preeditLength = 0;
    }

    if (preedit.empty()) {
        ime.edit_text.editLengthChange = 0;
        ime.caretIndex = ime.edit_text.caretIndex;
        ime.event_id = SCE_IME_EVENT_UPDATE_TEXT;
        return;
    }

    const uint32_t remaining = ime.param.maxTextLength > ime.str.size() ? ime.param.maxTextLength - static_cast<uint32_t>(ime.str.size()) : 0;
    const uint32_t preedit_len = bounded_index(preedit, remaining);
    if (preedit_len == 0)
        return;

    const std::u16string to_insert = preedit.substr(0, preedit_len);
    ime.edit_text.preeditIndex = ime.edit_text.caretIndex;
    ime.str.insert(ime.edit_text.caretIndex, to_insert);
    ime.edit_text.preeditLength = preedit_len;
    ime.edit_text.editLengthChange = static_cast<int32_t>(preedit_len);
    ime.edit_text.caretIndex += preedit_len;

    ime.event_id = SCE_IME_EVENT_UPDATE_TEXT;
}

void ime_cursor_left(Ime &ime) {
    normalize_edit_state(ime);
    if (ime.edit_text.preeditLength > 0) {
        ime.str.erase(ime.edit_text.preeditIndex, ime.edit_text.preeditLength);
        ime.edit_text.caretIndex = ime.edit_text.preeditIndex;
        ime.edit_text.preeditLength = 0;
        ime.edit_text.editLengthChange = 0;
    }

    ime.edit_text.editIndex = ime.edit_text.caretIndex;
    if (ime.edit_text.caretIndex > 0)
        ime.edit_text.caretIndex = bounded_index(ime.str, ime.edit_text.caretIndex - 1);
    ime.caretIndex = ime.edit_text.caretIndex;
    ime.edit_text.preeditIndex = ime.edit_text.caretIndex;
    ime.event_id = SCE_IME_EVENT_UPDATE_CARET;
}

void ime_cursor_right(Ime &ime) {
    normalize_edit_state(ime);
    if (ime.edit_text.preeditLength > 0) {
        ime.str.erase(ime.edit_text.preeditIndex, ime.edit_text.preeditLength);
        ime.edit_text.caretIndex = ime.edit_text.preeditIndex;
        ime.edit_text.preeditLength = 0;
        ime.edit_text.editLengthChange = 0;
    }

    ime.edit_text.editIndex = ime.edit_text.caretIndex;
    if (ime.edit_text.caretIndex < ime.str.length()) {
        ++ime.edit_text.caretIndex;
        if (splits_surrogate(ime.str, ime.edit_text.caretIndex))
            ++ime.edit_text.caretIndex;
    }
    ime.caretIndex = ime.edit_text.caretIndex;
    ime.edit_text.preeditIndex = ime.edit_text.caretIndex;
    ime.event_id = SCE_IME_EVENT_UPDATE_CARET;
}

void ime_backspace(Ime &ime) {
    normalize_edit_state(ime);
    if (ime.edit_text.preeditLength > 0) {
        ime.str.erase(ime.edit_text.preeditIndex, ime.edit_text.preeditLength);
        ime.edit_text.caretIndex = ime.edit_text.preeditIndex;
        ime.edit_text.preeditLength = 0;
        ime.edit_text.editLengthChange = 0;
        ime.caretIndex = ime.edit_text.caretIndex;
        ime.event_id = SCE_IME_EVENT_UPDATE_TEXT;
        return;
    }

    if (ime.edit_text.caretIndex == 0)
        return;

    const uint32_t erase_start = bounded_index(ime.str, ime.edit_text.caretIndex - 1);
    ime.str.erase(erase_start, ime.edit_text.caretIndex - erase_start);
    ime.edit_text.editIndex = ime.edit_text.caretIndex;
    ime.edit_text.caretIndex = erase_start;
    ime.caretIndex = ime.edit_text.caretIndex;
    ime.edit_text.preeditIndex = ime.edit_text.caretIndex;
    ime.event_id = SCE_IME_EVENT_UPDATE_TEXT;
}

std::vector<std::pair<SceImeLanguage, std::string>>::const_iterator
get_ime_lang_index(Ime &ime, SceImeLanguage lang) {
    return std::find_if(ime.lang.ime_keyboards.begin(), ime.lang.ime_keyboards.end(),
        [&](const auto &l) { return l.first == lang; });
}
