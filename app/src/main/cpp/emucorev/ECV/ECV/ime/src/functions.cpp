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

namespace ime {

std::vector<std::pair<SceImeLanguage, std::string>>::const_iterator get_ime_lang_index(Ime &ime, const SceImeLanguage &lang) {
    return std::find_if(ime.lang.ime_keyboards.begin(), ime.lang.ime_keyboards.end(), [&](const auto &l) {
        return l.first == lang;
    });
}

void init_ime_lang(Ime &ime, const SceImeLanguage &lang) {
    ime.lang.second_keyboard.clear();
    switch (lang) {
    case SCE_IME_LANGUAGE_SPANISH:
        ime.lang.punct = { { 1, { u",", u"¿?" } }, { 2, { u".", u"¡!" } } };
        break;
    default:
        ime.lang.punct = { { 1, { u",", u"?" } }, { 2, { u".", u"!" } } };
        break;
    }

    switch (lang) {
    case SCE_IME_LANGUAGE_DANISH: ime.lang.space_str = "Mellemrum"; break;
    case SCE_IME_LANGUAGE_GERMAN: ime.lang.space_str = "Leerstelle"; break;
    case SCE_IME_LANGUAGE_SPANISH: ime.lang.space_str = "Espacio"; break;
    case SCE_IME_LANGUAGE_FRENCH: ime.lang.space_str = "Espace"; break;
    case SCE_IME_LANGUAGE_ITALIAN: ime.lang.space_str = "Spazio"; break;
    case SCE_IME_LANGUAGE_DUTCH: ime.lang.space_str = "Spatie"; break;
    case SCE_IME_LANGUAGE_NORWEGIAN: ime.lang.space_str = "Mellomrom"; break;
    case SCE_IME_LANGUAGE_POLISH: ime.lang.space_str = "Spacja"; break;
    case SCE_IME_LANGUAGE_PORTUGUESE_BR:
    case SCE_IME_LANGUAGE_PORTUGUESE_PT: ime.lang.space_str = reinterpret_cast<const char *>(u8"Espaço"); break;
    case SCE_IME_LANGUAGE_RUSSIAN: ime.lang.space_str = reinterpret_cast<const char *>(u8"П р о б е л"); break;
    case SCE_IME_LANGUAGE_FINNISH: ime.lang.space_str = reinterpret_cast<const char *>(u8"Välilyönti"); break;
    case SCE_IME_LANGUAGE_SWEDISH: ime.lang.space_str = "Blanksteg"; break;
    case SCE_IME_LANGUAGE_TURKISH: ime.lang.space_str = reinterpret_cast<const char *>(u8"Boşluk"); break;
    default: ime.lang.space_str = "Space"; break;
    }

    switch (lang) {
    case SCE_IME_LANGUAGE_FRENCH:
        ime.lang.keyboard_pos = { { 1, 13.f }, { 2, 13.f }, { 3, 201.f } };
        break;
    case SCE_IME_LANGUAGE_RUSSIAN:
        ime.lang.keyboard_pos = { { 1, 13.f }, { 2, 13.f }, { 3, 130.5f } };
        break;
    default:
        ime.lang.keyboard_pos = { { 1, 13.f }, { 2, 60.f }, { 3, 154.f } };
        break;
    }

    switch (lang) {
    case SCE_IME_LANGUAGE_GERMAN:
        ime.lang.key = {
            { 1, { u"q", u"w", u"e", u"r", u"t", u"z", u"u", u"i", u"o", u"p" } },
            { 2, { u"a", u"s", u"d", u"f", u"g", u"h", u"j", u"k", u"l" } },
            { 3, { u"y", u"x", u"c", u"v", u"b", u"n", u"m" } }
        };
        ime.lang.shift_key = {
            { 1, { u"Q", u"W", u"E", u"R", u"T", u"Z", u"U", u"I", u"O", u"P" } },
            { 2, { u"A", u"S", u"D", u"F", u"G", u"H", u"J", u"K", u"L" } },
            { 3, { u"Y", u"X", u"C", u"V", u"B", u"N", u"M" } }
        };
        break;
    case SCE_IME_LANGUAGE_FRENCH:
        ime.lang.key = {
            { 1, { u"a", u"z", u"e", u"r", u"t", u"y", u"u", u"i", u"o", u"p" } },
            { 2, { u"q", u"s", u"d", u"f", u"g", u"h", u"j", u"k", u"l", u"m" } },
            { 3, { u"w", u"x", u"c", u"v", u"b", u"n" } }
        };
        ime.lang.shift_key = {
            { 1, { u"A", u"Z", u"E", u"R", u"T", u"Y", u"U", u"I", u"O", u"P" } },
            { 2, { u"Q", u"S", u"D", u"F", u"G", u"H", u"J", u"K", u"L", u"M" } },
            { 3, { u"W", u"X", u"C", u"V", u"B", u"N" } }
        };
        break;
    case SCE_IME_LANGUAGE_RUSSIAN:
        ime.lang.second_keyboard = { { 1, "ABC" }, { 2, reinterpret_cast<const char *>(u8"РУ") } };
        ime.lang.key = {
            { 1, { u"й", u"ц", u"у", u"к", u"е", u"н", u"г", u"ш", u"щ", u"з", u"х", u"ъ" } },
            { 2, { u"ё", u"ф", u"ы", u"в", u"а", u"п", u"р", u"о", u"л", u"д", u"ж", u"э" } },
            { 3, { u"я", u"ч", u"с", u"м", u"и", u"т", u"ь", u"б", u"ю" } }
        };
        ime.lang.shift_key = {
            { 1, { u"Й", u"Ц", u"У", u"К", u"Е", u"Н", u"Г", u"Ш", u"Щ", u"З", u"Х", u"Ъ" } },
            { 2, { u"Ё", u"Ф", u"Ы", u"В", u"А", u"П", u"Р", u"О", u"Л", u"Д", u"Ж", u"Э" } },
            { 3, { u"Я", u"Ч", u"С", u"М", u"И", u"Т", u"Ь", u"Б", u"Ю" } }
        };
        break;
    default:
        ime.lang.key = {
            { 1, { u"q", u"w", u"e", u"r", u"t", u"y", u"u", u"i", u"o", u"p" } },
            { 2, { u"a", u"s", u"d", u"f", u"g", u"h", u"j", u"k", u"l" } },
            { 3, { u"z", u"x", u"c", u"v", u"b", u"n", u"m" } }
        };
        ime.lang.shift_key = {
            { 1, { u"Q", u"W", u"E", u"R", u"T", u"Y", u"U", u"I", u"O", u"P" } },
            { 2, { u"A", u"S", u"D", u"F", u"G", u"H", u"J", u"K", u"L" } },
            { 3, { u"Z", u"X", u"C", u"V", u"B", u"N", u"M" } }
        };
        break;
    }

    switch (lang) {
    case SCE_IME_LANGUAGE_RUSSIAN:
        ime.lang.size_button = 111.5f;
        ime.lang.size_key = 868.f / 12.f;
        break;
    default:
        ime.lang.size_button = 135.f;
        ime.lang.size_key = 88.f;
        break;
    }
}

} // namespace ime
