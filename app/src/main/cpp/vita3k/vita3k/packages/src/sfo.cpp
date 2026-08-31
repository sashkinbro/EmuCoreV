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

/**
 * @file sfo.cpp
 * @brief PlayStation setting file (`.sfo`) handling
 *
 * PlayStation setting files (`.sfo`) contain metadata information usually describing
 * the content they are accompanying.
 */

#include <packages/sfo.h>

#include <util/log.h>

#include <boost/algorithm/string/trim.hpp>

#include <algorithm>
#include <cstring>
#include <fmt/format.h>

namespace sfo {

bool get_data_by_id(std::string &out_data, SfoFile &file, int id) {
    std::string key;
    switch (id) {
    case 6:
        key = "CONTENT_ID";
        break;
    case 7:
        key = "NP_COMMUNICATION_ID";
        break;
    case 8:
        key = "CATEGORY";
        break;
    case 9:
        key = "TITLE";
        break;
    case 10:
        key = "STITLE";
        break;
    case 0xc:
        key = "TITLE_ID";
        break;
    case 0xe: // Todo
    default:
        return false;
    }

    return get_data_by_key(out_data, file, key);
}

bool get_data_by_key(std::string &out_data, SfoFile &file, const std::string &key) {
    auto res = std::find_if(file.entries.begin(), file.entries.end(),
        [key](const auto &et) { return et.data.first == key; });

    if (res == file.entries.end()) {
        return false;
    }
    out_data = res->data.second;

    return true;
}

bool get_param_info(sfo::SfoAppInfo &app_info, const vfs::FileBuffer &param, int sys_lang) {
    SfoFile sfo_handle;
    if (!sfo::load(sfo_handle, param))
        return false;
    sfo::get_data_by_key(app_info.app_version, sfo_handle, "APP_VER");
    if (!app_info.app_version.empty() && app_info.app_version[0] == '0')
        app_info.app_version.erase(app_info.app_version.begin());
    sfo::get_data_by_key(app_info.app_category, sfo_handle, "CATEGORY");
    sfo::get_data_by_key(app_info.app_content_id, sfo_handle, "CONTENT_ID");
    if (!sfo::get_data_by_key(app_info.app_addcont, sfo_handle, "INSTALL_DIR_ADDCONT"))
        sfo::get_data_by_key(app_info.app_addcont, sfo_handle, "TITLE_ID");
    if (!sfo::get_data_by_key(app_info.app_savedata, sfo_handle, "INSTALL_DIR_SAVEDATA"))
        sfo::get_data_by_key(app_info.app_savedata, sfo_handle, "TITLE_ID");
    sfo::get_data_by_key(app_info.app_parental_level, sfo_handle, "PARENTAL_LEVEL");
    if (!sfo::get_data_by_key(app_info.app_short_title, sfo_handle, fmt::format("STITLE_{:0>2d}", sys_lang)))
        sfo::get_data_by_key(app_info.app_short_title, sfo_handle, "STITLE");
    if (!sfo::get_data_by_key(app_info.app_title, sfo_handle, fmt::format("TITLE_{:0>2d}", sys_lang)))
        sfo::get_data_by_key(app_info.app_title, sfo_handle, "TITLE");
    std::replace(app_info.app_title.begin(), app_info.app_title.end(), '\n', ' ');
    boost::trim(app_info.app_title);
    sfo::get_data_by_key(app_info.app_title_id, sfo_handle, "TITLE_ID");
    return true;
}

bool load(SfoFile &sfile, const std::vector<uint8_t> &content) {
    if (content.size() < sizeof(SfoHeader))
        return false;

    SfoFile parsed{};
    memcpy(&parsed.header, content.data(), sizeof(SfoHeader));
    const auto &header = parsed.header;
    if (header.magic != 0x46535000) {
        LOG_ERROR("param.sfo rejected: bad magic 0x{:08X} (expected 0x46535000)", header.magic);
        return false;
    }

    // Validate offsets before allocating entries or constructing any iterators.
    if (header.tables_entries > (content.size() - sizeof(SfoHeader)) / sizeof(SfoIndexTableEntry))
        return false;
    const size_t index_end = sizeof(SfoHeader) + size_t(header.tables_entries) * sizeof(SfoIndexTableEntry);
    if (header.key_table_start < index_end || header.data_table_start < header.key_table_start
        || header.data_table_start > content.size())
        return false;

    parsed.entries.resize(header.tables_entries);
    for (size_t i = 0; i < parsed.entries.size(); ++i) {
        auto &item = parsed.entries[i];
        memcpy(&item.entry, content.data() + sizeof(SfoHeader) + i * sizeof(SfoIndexTableEntry), sizeof(SfoIndexTableEntry));
        const auto &entry = item.entry;
        const size_t key_size = header.data_table_start - header.key_table_start;
        if (entry.key_offset >= key_size)
            return false;
        const auto key_begin = content.begin() + header.key_table_start + entry.key_offset;
        const auto key_limit = content.begin() + header.data_table_start;
        const auto key_end = std::find(key_begin, key_limit, uint8_t(0));
        if (key_end == key_begin || key_end == key_limit)
            return false;
        item.data.first.assign(key_begin, key_end);

        const size_t data_size = content.size() - header.data_table_start;
        if (entry.data_offset > data_size || entry.data_len > data_size - entry.data_offset
            || entry.data_len > entry.data_max_len)
            return false;
        const auto data_begin = content.begin() + header.data_table_start + entry.data_offset;
        const auto data_end = data_begin + entry.data_len;
        switch (entry.data_fmt) {
        case SfoDataFormat::UINT32_T: {
            if (entry.data_len != sizeof(uint32_t))
                return false;
            uint32_t value;
            memcpy(&value, &*data_begin, sizeof(value));
            item.data.second = std::to_string(value);
            break;
        }
        case SfoDataFormat::ASCII:
        case SfoDataFormat::UTF8:
            item.data.second.assign(data_begin, data_end);
            break;
        case SfoDataFormat::UTF8_NULL:
            if (entry.data_len == 0 || *(data_end - 1) != 0)
                return false;
            item.data.second.assign(data_begin, std::find(data_begin, data_end, uint8_t(0)));
            break;
        default:
            return false;
        }
    }

    sfile = std::move(parsed);
    return true;
}

} // namespace sfo
