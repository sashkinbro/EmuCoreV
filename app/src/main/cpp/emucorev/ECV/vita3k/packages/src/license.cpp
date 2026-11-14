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
 * @file license.cpp
 * @brief Sony `NpDrm` license and `work.bin` handling
 */

#include <emuenv/state.h>

#include <packages/license.h>

#include <util/bytes.h>
#include <util/log.h>

#include <zRIF/keyflate.h>
#include <zrif2rif.h>

#include <b64/cdecode.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iterator>
#include <utility>

bool validate_zrif(const std::string &zRIF) {
    if (zRIF.empty())
        return false;

    char buf[2048];
    base64_decodestate state;
    base64_init_decodestate(&state);
    const size_t decoded_len = base64_decode_block(zRIF.c_str(), static_cast<int>(zRIF.size()), buf, &state);
    if (decoded_len == 0)
        return false;

    uint8_t license[512] = {};
    const int inflated_len = inflateKey(reinterpret_cast<const uint8_t *>(buf), decoded_len, license, sizeof(license));
    return inflated_len == 512;
}

static bool open_license(const fs::path &license_path, SceNpDrmLicense &license_buf) {
    memset(&license_buf, 0, sizeof(SceNpDrmLicense));
    fs::ifstream license(license_path, std::ios::in | std::ios::binary);
    if (license.is_open()) {
        license.read((char *)&license_buf, sizeof(SceNpDrmLicense));
        const bool complete = license.gcount() == sizeof(SceNpDrmLicense);
        license.close();
        return complete;
    }

    return false;
}

static bool get_license_content_ids(const SceNpDrmLicense &license_buf, std::string &content_id, std::string &title_id) {
    const auto content_id_end = std::find(std::begin(license_buf.content_id), std::end(license_buf.content_id), '\0');
    if (content_id_end == std::end(license_buf.content_id))
        return false;

    const std::string parsed_content_id(license_buf.content_id, content_id_end);
    // Vita content IDs have a six-character publisher, a nine-character title
    // ID, a two-digit content index and a sixteen-character product suffix.
    // The content index can vary (for example, updates need not use _00-).
    if (parsed_content_id.size() != 36 || parsed_content_id[6] != '-' ||
        parsed_content_id[16] != '_' || !std::isdigit(static_cast<unsigned char>(parsed_content_id[17])) ||
        !std::isdigit(static_cast<unsigned char>(parsed_content_id[18])) || parsed_content_id[19] != '-')
        return false;

    const auto is_filename_id_char = [](char value) {
        return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
            (value >= '0' && value <= '9') || value == '_' || value == '-';
    };
    if (!std::all_of(parsed_content_id.begin(), parsed_content_id.begin() + 6, is_filename_id_char) ||
        !std::all_of(parsed_content_id.begin() + 7, parsed_content_id.begin() + 16, is_filename_id_char) ||
        !std::all_of(parsed_content_id.begin() + 20, parsed_content_id.end(), is_filename_id_char))
        return false;

    const auto title_begin = parsed_content_id.begin() + 7;
    if (!std::all_of(title_begin, title_begin + 4, [](char value) { return value >= 'A' && value <= 'Z'; }) ||
        !std::all_of(title_begin + 4, title_begin + 9, [](char value) { return value >= '0' && value <= '9'; }))
        return false;

    content_id = parsed_content_id;
    title_id = parsed_content_id.substr(7, 9);
    return true;
}

bool copy_license(EmuEnvState &emuenv, const fs::path &license_path) {
    SceNpDrmLicense license_buf;
    if (open_license(license_path, license_buf)) {
        std::string content_id;
        std::string title_id;
        if (!get_license_content_ids(license_buf, content_id, title_id)) {
            LOG_ERROR("License file has an invalid content ID: {}", license_path);
            return false;
        }

        const auto dst_path{ emuenv.vita_fs_path / "ux0/license" / title_id };
        fs::create_directories(dst_path);

        const auto license_dst_path{ dst_path / fmt::format("{}.rif", content_id) };
        if (license_path != license_dst_path) {
            fs::copy_file(license_path, license_dst_path, fs::copy_options::overwrite_existing);
            if (fs::exists(license_dst_path)) {
                emuenv.license_content_id = std::move(content_id);
                emuenv.license_title_id = std::move(title_id);
                LOG_INFO("Success copy license file to: {}", license_dst_path);
                return true;
            } else
                LOG_ERROR("Fail copy license file to: {}", license_dst_path);
        } else
            LOG_ERROR("Source and destination license is same at: {}", license_path);
    } else
        LOG_ERROR("License file is corrupted at: {}", license_path);

    return false;
}

void get_license(EmuEnvState &emuenv, const std::string &title_id, const std::string &content_id) {
    // Skip if it's not a retail game or already have a license
    if (!title_id.starts_with("PCS") || emuenv.license.rif.contains(title_id))
        return;

    // Get license buffer corresponding to the title id
    auto &license_buf = emuenv.license.rif[title_id];
    license_buf = {};

    // Open license file
    const auto license_path{ emuenv.vita_fs_path / "ux0/license" / title_id / fmt::format("{}.rif", content_id) };
    if (!open_license(license_path, license_buf)) {
        if (fs::exists(license_path))
            fs::remove(license_path);

        LOG_WARN("License file is corrupted or missing at: {}, using default value.", license_path);
        const auto RETAIL_APP_PATH{ emuenv.vita_fs_path / "ux0/app" / title_id / "sce_sys/retail/livearea" };
        if (fs::exists(RETAIL_APP_PATH))
            license_buf.sku_flag = 1;
        else
            license_buf.sku_flag = 0;
    } else
        license_buf.sku_flag = byte_swap(license_buf.sku_flag); // Convert to little endian
}

bool create_license(EmuEnvState &emuenv, const std::string &zRIF) {
    if (!validate_zrif(zRIF)) {
        LOG_ERROR("Invalid zRIF key provided");
        return false;
    }

    fs::create_directories(emuenv.cache_path);

    // Create a temp license file
    const auto temp_license_path = emuenv.cache_path / "temp_licence.rif";
    fs::ofstream temp_file(temp_license_path, std::ios::out | std::ios::binary);
    if (!temp_file.is_open()) {
        LOG_ERROR("Failed to create temp license file at: {}", temp_license_path);
        return false;
    }

    // Convert zRIF to RIF
    zrif2rif(zRIF, temp_file);
    auto res = copy_license(emuenv, temp_license_path);
    fs::remove(temp_license_path);
    return res;
}
