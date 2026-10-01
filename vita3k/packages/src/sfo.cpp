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

void get_param_info(sfo::SfoAppInfo &app_info, const vfs::FileBuffer &param, int sys_lang) {
    SfoFile sfo_handle;
    sfo::load(sfo_handle, param);
    sfo::get_data_by_key(app_info.app_version, sfo_handle, "APP_VER");
    if (app_info.app_version[0] == '0')
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
}

bool load(SfoFile &sfile, const std::vector<uint8_t> &content) {
    sfile = {};
    if (content.size() < sizeof(SfoHeader))
        return false;
    SfoFile parsed{};
    std::memcpy(&parsed.header, content.data(), sizeof(SfoHeader));
    const auto &header = parsed.header;
    if (header.magic != 0x46535000 || header.key_table_start < sizeof(SfoHeader)
        || header.key_table_start > header.data_table_start || header.data_table_start > content.size()
        || header.tables_entries > (header.key_table_start - sizeof(SfoHeader)) / sizeof(SfoIndexTableEntry))
        return false;

    parsed.entries.reserve(header.tables_entries);
    for (uint32_t i = 0; i < header.tables_entries; ++i) {
        SfoFile::SfoEntry item{};
        std::memcpy(&item.entry, content.data() + sizeof(SfoHeader) + i * sizeof(SfoIndexTableEntry), sizeof(SfoIndexTableEntry));
        const auto &entry = item.entry;
        const size_t key_start = static_cast<size_t>(header.key_table_start) + entry.key_offset;
        const size_t data_start = static_cast<size_t>(header.data_table_start) + entry.data_offset;
        if (key_start >= header.data_table_start || data_start > content.size()
            || entry.data_len > entry.data_max_len || entry.data_len > content.size() - data_start)
            return false;
        const auto key = content.begin() + key_start;
        const auto key_end = std::find(key, content.begin() + header.data_table_start, 0);
        if (key_end == content.begin() + header.data_table_start || key_end == key)
            return false;
        item.data.first.assign(key, key_end);
        const auto data = content.begin() + data_start;
        switch (entry.data_fmt) {
        case SfoDataFormat::UINT32_T: {
            if (entry.data_len != sizeof(uint32_t))
                return false;
            uint32_t value;
            std::memcpy(&value, content.data() + data_start, sizeof(value));
            item.data.second = std::to_string(value);
            break;
        }
        case SfoDataFormat::ASCII:
        case SfoDataFormat::UTF8:
            item.data.second.assign(data, data + entry.data_len);
            break;
        case SfoDataFormat::UTF8_NULL:
            if (!entry.data_len || *(data + entry.data_len - 1) != 0)
                return false;
            item.data.second.assign(data, data + entry.data_len - 1);
            break;
        default:
            return false;
        }
        parsed.entries.push_back(std::move(item));
    }
    sfile = std::move(parsed);
    return true;
}

} // namespace sfo
