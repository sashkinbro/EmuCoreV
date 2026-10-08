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
 * @file pkg.cpp
 * @brief PlayStation Vita software package (`.pkg`) handling
 */

#include <F00DKeyEncryptorFactory.h>
#include <CryptoOperationsFactory.h>
#include <PfsFilesystem.h>
#include <PsvPfsParserConfig.h>
#include <Utils.h>
#include <openssl/evp.h>
#include <rif2zrif.h>

#include <io/functions.h>

#include <config/state.h>
#include <emuenv/state.h>

#include <packages/functions.h>
#include <packages/license.h>
#include <packages/pkg.h>
#include <packages/sce_types.h>
#include <packages/sfo.h>

#include <util/bytes.h>
#include <util/log.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

// Credits to mmozeiko https://github.com/mmozeiko/pkg2zip

int extract_klicensee(const PsvPfsParserConfig &cfg, std::shared_ptr<ICryptoOperations> cryptops, unsigned char *klicensee);
std::shared_ptr<IF00DKeyEncryptor> create_F00D_encryptor(const PsvPfsParserConfig &cfg, std::shared_ptr<ICryptoOperations> cryptops);

static void ctr_init(uint8_t *counter, uint8_t *iv, uint64_t n) {
    for (int i = 15; i >= 0; i--) {
        n = n + iv[i];
        counter[i] = (uint8_t)n;
        n >>= 8;
    }
}

static int execute(std::string &zrif, fs::path &title_src, fs::path &title_dst, F00DEncryptorTypes type, std::string &f00d_arg, PfsProgressCallback progress = nullptr) {
    std::string title_src_str = title_src.string();
    std::string title_dst_str = title_dst.string();
    return execute(zrif, title_src_str, title_dst_str, type, f00d_arg, progress);
}

namespace {

using PkgFileHandle = std::unique_ptr<FILE, decltype(&fclose)>;

struct RemovePathOnExit {
    fs::path path;
    bool active = true;

    ~RemovePathOnExit() {
        if (active) {
            boost::system::error_code error;
            fs::remove_all(path, error);
        }
    }

    void release() { active = false; }
};

static bool seek_to(FILE *file, uint64_t offset) {
    if (offset > static_cast<uint64_t>(LONG_MAX))
        return false;
    return fseek(file, static_cast<long>(offset), SEEK_SET) == 0;
}

static bool read_exact(FILE *file, uint64_t offset, void *buffer, size_t size) {
    return seek_to(file, offset) && fread(buffer, 1, size, file) == size;
}

static bool range_within(uint64_t offset, uint64_t size, uint64_t total_size) {
    return offset <= total_size && size <= total_size - offset;
}

static bool safe_install_component(const std::string &value) {
    return !value.empty() && value != "." && value != ".." &&
        value.find_first_of("/\\:") == std::string::npos && value.find('\0') == std::string::npos;
}

static std::optional<fs::path> safe_pkg_entry_path(std::string name) {
    if (name.empty() || name.find('\0') != std::string::npos)
        return std::nullopt;
    std::replace(name.begin(), name.end(), '\\', '/');
    if (name.front() == '/' || (name.size() >= 2 && name[1] == ':'))
        return std::nullopt;

    std::vector<std::string> components;
    size_t start = 0;
    while (start <= name.size()) {
        const size_t separator = name.find('/', start);
        const size_t end = separator == std::string::npos ? name.size() : separator;
        const std::string component = name.substr(start, end - start);
        if (component == "..")
            return std::nullopt;
        if (component.find(':') != std::string::npos)
            return std::nullopt;
        if (!component.empty() && component != ".")
            components.push_back(component);
        if (separator == std::string::npos)
            break;
        start = separator + 1;
    }
    if (components.empty())
        return std::nullopt;

    fs::path output;
    for (const auto &component : components)
        output /= fs_utils::utf8_to_path(component);
    return output;
}

static bool commit_staged_directory(const fs::path &staging_path, const fs::path &output_path) {
    const fs::path backup_path = output_path.parent_path() /
        (output_path.filename().string() + ".backup-" + fs::unique_path().string());
    bool moved_existing = false;

    try {
        if (fs::exists(output_path)) {
            fs::rename(output_path, backup_path);
            moved_existing = true;
        }
        fs::rename(staging_path, output_path);
    } catch (const fs::filesystem_error &error) {
        LOG_ERROR("Unable to commit PKG install at {}: {}", output_path, error.what());
        if (moved_existing && !fs::exists(output_path)) {
            try {
                fs::rename(backup_path, output_path);
            } catch (const fs::filesystem_error &restore_error) {
                LOG_CRITICAL("Unable to restore previous install at {}: {}", output_path, restore_error.what());
            }
        }
        return false;
    }

    if (moved_existing) {
        boost::system::error_code error;
        fs::remove_all(backup_path, error);
        if (error)
            LOG_WARN("Installed PKG but could not remove backup {}: {}", backup_path, error.message());
    }
    return true;
}

static bool merge_pkg_patch_into_app(EmuEnvState &emuenv, const fs::path &patch_path) {
    const fs::path app_path = emuenv.vita_fs_path / "ux0/app" / emuenv.app_info.app_title_id;
    const fs::path staging_path = app_path.parent_path() /
        (app_path.filename().string() + ".patch-" + fs::unique_path().string());
    if (!fs::create_directory(staging_path)) {
        LOG_ERROR("Unable to create PKG patch staging directory {}", staging_path);
        return false;
    }
    RemovePathOnExit cleanup{ staging_path };

    if (!fs_utils::copy_directory_contents(app_path, staging_path) ||
        !fs_utils::copy_directory_contents(patch_path, staging_path)) {
        LOG_ERROR("Unable to merge PKG patch into staged app {}", staging_path);
        return false;
    }
    if (!commit_staged_directory(staging_path, app_path))
        return false;
    cleanup.release();
    return true;
}

static bool decrypt_theme_pfs(
    std::string &zrif,
    const fs::path &source_path,
    const fs::path &destination_path,
    F00DEncryptorTypes type,
    std::string &f00d_arg) {
    PsvPfsParserConfig cfg;
    cfg.zRIF = zrif;
    cfg.title_id_src = source_path.string();
    cfg.title_id_dst = destination_path.string();
    cfg.f00d_enc_type = type;
    cfg.f00d_arg = f00d_arg;

    const auto cryptops = CryptoOperationsFactory::create(CryptoOperationsTypes::openssl);
    if (!cryptops)
        return false;
    const auto f00d = create_F00D_encryptor(cfg, cryptops);
    if (!f00d)
        return false;

    unsigned char klicensee[0x10]{};
    if (extract_klicensee(cfg, cryptops, klicensee) < 0)
        return false;

    const psvpfs::path source{ cfg.title_id_src };
    const psvpfs::path destination{ cfg.title_id_dst };
    PfsFilesystem pfs(cryptops, f00d, std::cout, klicensee, source);
    if (pfs.mount() < 0)
        return false;
    return pfs.decrypt_files(destination) >= 0;
}

} // namespace

bool decrypt_install_nonpdrm(EmuEnvState &emuenv, const fs::path &drmlicpath, const fs::path &title_path, const std::function<void(float)> &progress_callback) {
    fs::path title_id_src = title_path;
    fs::path title_id_dst = fs_utils::path_concat(title_path, "_dec");
    fs::ifstream binfile(drmlicpath, std::ios::in | std::ios::binary | std::ios::ate);
    std::string zRIF = rif2zrif(binfile);
    F00DEncryptorTypes f00d_enc_type = F00DEncryptorTypes::native;
    std::string f00d_arg = std::string();

    PfsProgressCallback pfs_progress = nullptr;
    if (progress_callback) {
        pfs_progress = [&progress_callback](std::uint64_t processed, std::uint64_t total, const std::string &) {
            progress_callback(total ? static_cast<float>(processed) / static_cast<float>(total) : 1.f);
        };
    }

    if ((execute(zRIF, title_id_src, title_id_dst, f00d_enc_type, f00d_arg, pfs_progress) < 0) && (title_path.string().find("theme") == std::string::npos))
        return false;

    if (!emuenv.app_info.app_category.contains("gp"))
        copy_license(emuenv, drmlicpath);

    fs::remove_all(title_id_src);
    fs::rename(title_id_dst, title_id_src);

    return true;
}

bool install_pkg(const fs::path &pkg_path, EmuEnvState &emuenv, std::string &p_zRIF, const std::function<void(float)> &progress_callback) {
    const auto report_progress = [&](float progress) {
        if (progress_callback)
            progress_callback(progress);
    };

    PkgFileHandle infile(FOPEN(pkg_path.c_str(), "rb"), &fclose);
    if (!infile) {
        LOG_CRITICAL("Failed to load pkg file in path: {}", fs_utils::path_to_utf8(pkg_path));
        return false;
    }

    boost::system::error_code file_size_error;
    const uint64_t pkg_size = fs::file_size(pkg_path, file_size_error);
    if (file_size_error || pkg_size < sizeof(PkgHeader) + sizeof(PkgExtHeader)) {
        LOG_ERROR("PKG is missing a complete header: {}", fs_utils::path_to_utf8(pkg_path));
        return false;
    }

    PkgHeader pkg_header{};
    PkgExtHeader ext_header{};
    if (!read_exact(infile.get(), 0, &pkg_header, sizeof(pkg_header)) ||
        !read_exact(infile.get(), sizeof(pkg_header), &ext_header, sizeof(ext_header))) {
        LOG_ERROR("Unable to read complete PKG headers");
        return false;
    }

    report_progress(0);
    if (byte_swap(pkg_header.magic) != 0x7F504b47 || byte_swap(ext_header.magic) != 0x7F657874) {
        LOG_ERROR("Not a valid PKG file");
        return false;
    }

    const uint64_t declared_size = byte_swap(pkg_header.total_size);
    const uint64_t data_offset = byte_swap(pkg_header.data_offset);
    const uint64_t data_size = byte_swap(pkg_header.data_size);
    const uint32_t info_offset = byte_swap(pkg_header.info_offset);
    const uint32_t info_count = byte_swap(pkg_header.info_count);
    const uint32_t file_count = byte_swap(pkg_header.file_count);
    if (declared_size < sizeof(PkgHeader) + sizeof(PkgExtHeader) || declared_size > pkg_size ||
        data_offset < sizeof(PkgHeader) + sizeof(PkgExtHeader) || !range_within(data_offset, data_size, declared_size) ||
        info_offset > declared_size || info_count > (declared_size - info_offset) / (2 * sizeof(uint32_t))) {
        LOG_ERROR("PKG header contains out-of-range offsets or sizes");
        return false;
    }

    uint32_t content_type = 0;
    uint32_t sfo_offset = 0;
    uint32_t sfo_size = 0;
    uint32_t items_offset = 0;
    bool found_content_type = false;
    bool found_sfo = false;
    bool found_items = false;
    uint64_t info_cursor = info_offset;
    for (uint32_t i = 0; i < info_count; i++) {
        uint32_t record_header[2]{};
        if (!range_within(info_cursor, sizeof(record_header), declared_size) ||
            !read_exact(infile.get(), info_cursor, record_header, sizeof(record_header))) {
            LOG_ERROR("PKG info record {} is truncated", i);
            return false;
        }
        const uint32_t record_type = byte_swap(record_header[0]);
        const uint32_t record_size = byte_swap(record_header[1]);
        const uint64_t payload_offset = info_cursor + sizeof(record_header);
        if (!range_within(payload_offset, record_size, declared_size)) {
            LOG_ERROR("PKG info record {} extends past the package", i);
            return false;
        }

        uint32_t values[2]{};
        if (record_type == 2 || record_type == 13 || record_type == 14) {
            const uint32_t required_size = record_type == 14 ? 2 * sizeof(uint32_t) : sizeof(uint32_t);
            if (record_size < required_size || !read_exact(infile.get(), payload_offset, values, required_size)) {
                LOG_ERROR("PKG info record {} has an incomplete payload", i);
                return false;
            }
            if (record_type == 2) {
                content_type = byte_swap(values[0]);
                found_content_type = true;
            } else if (record_type == 13) {
                items_offset = byte_swap(values[0]);
                found_items = true;
            } else {
                sfo_offset = byte_swap(values[0]);
                sfo_size = byte_swap(values[1]);
                found_sfo = true;
            }
        }
        info_cursor = payload_offset + record_size;
    }

    if (!found_content_type || !found_sfo || sfo_size == 0 || sfo_size > 16 * 1024 * 1024 ||
        !range_within(sfo_offset, sfo_size, declared_size)) {
        LOG_ERROR("PKG is missing valid content-type or SFO metadata");
        return false;
    }
    if (file_count != 0 && !found_items) {
        LOG_ERROR("PKG has files but no file-table offset");
        return false;
    }

    const uint64_t table_offset = static_cast<uint64_t>(items_offset);
    if (file_count != 0 && (table_offset % 16 != 0 || table_offset > data_size ||
            file_count > (data_size - table_offset) / sizeof(PkgEntry))) {
        LOG_ERROR("PKG file table is misaligned or extends past the package");
        return false;
    }

    PkgType type;
    switch (content_type) {
    case 0x15: type = PkgType::PKG_TYPE_VITA_APP; break;
    case 0x16: type = PkgType::PKG_TYPE_VITA_DLC; break;
    case 0x1F: type = PkgType::PKG_TYPE_VITA_THEME; break;
    default:
        LOG_ERROR("Unsupported content type: {}", content_type);
        return false;
    }

    const auto key_type = byte_swap(ext_header.data_type2) & 7;
    const uint8_t *pkg_vita_key = nullptr;
    switch (key_type) {
    case 2: pkg_vita_key = pkg_vita_2; break;
    case 3: pkg_vita_key = pkg_vita_3; break;
    case 4: pkg_vita_key = pkg_vita_4; break;
    default:
        LOG_ERROR("Unknown PKG encryption key type {}", key_type);
        return false;
    }

    using CipherContextPtr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
    using CipherPtr = std::unique_ptr<EVP_CIPHER, decltype(&EVP_CIPHER_free)>;
    CipherContextPtr cipher_ctx(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
    CipherPtr cipher_ctr(EVP_CIPHER_fetch(nullptr, "AES-128-CTR", nullptr), &EVP_CIPHER_free);
    CipherPtr cipher_ecb(EVP_CIPHER_fetch(nullptr, "AES-128-ECB", nullptr), &EVP_CIPHER_free);
    if (!cipher_ctx || !cipher_ctr || !cipher_ecb) {
        LOG_ERROR("Unable to initialize PKG decryption ciphers");
        return false;
    }

    uint8_t main_key[16]{};
    int output_size = 0;
    if (EVP_EncryptInit_ex(cipher_ctx.get(), cipher_ecb.get(), nullptr, pkg_vita_key, nullptr) != 1 ||
        EVP_CIPHER_CTX_set_padding(cipher_ctx.get(), 0) != 1 ||
        EVP_EncryptUpdate(cipher_ctx.get(), main_key, &output_size, pkg_header.pkg_data_iv, sizeof(pkg_header.pkg_data_iv)) != 1 ||
        output_size != sizeof(main_key) ||
        EVP_EncryptFinal_ex(cipher_ctx.get(), main_key + output_size, &output_size) != 1) {
        LOG_ERROR("Unable to derive PKG encryption key");
        return false;
    }

    std::vector<uint8_t> sfo_buffer(sfo_size);
    if (!read_exact(infile.get(), sfo_offset, sfo_buffer.data(), sfo_buffer.size())) {
        LOG_ERROR("Unable to read complete PKG SFO metadata");
        return false;
    }
    emuenv.app_info = {};
    if (!sfo::get_param_info(emuenv.app_info, sfo_buffer, emuenv.cfg.sys_lang)) {
        LOG_ERROR("Unable to parse PKG SFO metadata");
        return false;
    }

    if (type != PkgType::PKG_TYPE_VITA_THEME && !safe_install_component(emuenv.app_info.app_title_id)) {
        LOG_ERROR("PKG contains an unsafe title ID");
        return false;
    }
    if (type == PkgType::PKG_TYPE_VITA_DLC) {
        if (emuenv.app_info.app_content_id.size() <= 20) {
            LOG_ERROR("PKG DLC content ID is too short");
            return false;
        }
        emuenv.app_info.app_content_id = emuenv.app_info.app_content_id.substr(20);
        if (!safe_install_component(emuenv.app_info.app_content_id)) {
            LOG_ERROR("PKG contains an unsafe DLC content ID");
            return false;
        }
    } else if (type == PkgType::PKG_TYPE_VITA_THEME && !safe_install_component(emuenv.app_info.app_content_id)) {
        LOG_ERROR("PKG contains an unsafe theme content ID");
        return false;
    }

    if (type == PkgType::PKG_TYPE_VITA_APP && emuenv.app_info.app_category == "gp")
        type = PkgType::PKG_TYPE_VITA_PATCH;

    fs::path output_path = emuenv.vita_fs_path / "ux0";
    switch (type) {
    case PkgType::PKG_TYPE_VITA_APP:
        output_path /= fs::path("app") / emuenv.app_info.app_title_id;
        emuenv.app_info.app_title += " (App)";
        break;
    case PkgType::PKG_TYPE_VITA_DLC:
        output_path /= fs::path("addcont") / emuenv.app_info.app_title_id / emuenv.app_info.app_content_id;
        emuenv.app_info.app_title += " (DLC)";
        break;
    case PkgType::PKG_TYPE_VITA_PATCH:
        output_path /= fs::path("patch") / emuenv.app_info.app_title_id;
        emuenv.app_info.app_title += " (Update)";
        if (!fs::exists(emuenv.vita_fs_path / "ux0/app" / emuenv.app_info.app_title_id)) {
            LOG_ERROR("Install app before its PKG patch");
            return false;
        }
        break;
    case PkgType::PKG_TYPE_VITA_THEME:
        output_path /= fs::path("theme") / emuenv.app_info.app_content_id;
        emuenv.app_info.app_category = "theme";
        emuenv.app_info.app_title += " (Theme)";
        break;
    }

    boost::system::error_code filesystem_error;
    fs::create_directories(output_path.parent_path(), filesystem_error);
    if (filesystem_error || !fs::is_directory(output_path.parent_path())) {
        LOG_ERROR("Unable to create PKG install parent {}", output_path.parent_path());
        return false;
    }
    const fs::path staging_path = output_path.parent_path() /
        (output_path.filename().string() + ".pkg-" + fs::unique_path().string());
    if (!fs::create_directory(staging_path)) {
        LOG_ERROR("Unable to create PKG staging directory {}", staging_path);
        return false;
    }
    RemovePathOnExit staging_cleanup{ staging_path };
    const fs::path decrypted_path = fs_utils::path_concat(staging_path, "_dec");
    RemovePathOnExit decrypted_cleanup{ decrypted_path };

    const auto decrypt_ctr = [&](uint64_t byte_offset, uint8_t *data, size_t size) {
        if (size > static_cast<size_t>(INT_MAX))
            return false;
        uint8_t counter[0x10];
        ctr_init(counter, pkg_header.pkg_data_iv, byte_offset / 16);
        int first_size = 0;
        int final_size = 0;
        return EVP_DecryptInit_ex(cipher_ctx.get(), cipher_ctr.get(), nullptr, main_key, counter) == 1 &&
            EVP_CIPHER_CTX_set_padding(cipher_ctx.get(), 0) == 1 &&
            EVP_DecryptUpdate(cipher_ctx.get(), data, &first_size, data, static_cast<int>(size)) == 1 &&
            EVP_DecryptFinal_ex(cipher_ctx.get(), data + first_size, &final_size) == 1 &&
            static_cast<size_t>(first_size + final_size) == size;
    };

    std::vector<uint8_t> buffer(0x10000);
    std::unordered_set<std::string> extracted_paths;
    for (uint32_t i = 0; i < file_count; i++) {
        const uint64_t entry_offset = table_offset + static_cast<uint64_t>(i) * sizeof(PkgEntry);
        const uint64_t entry_file_offset = data_offset + entry_offset;
        PkgEntry entry{};
        if (!read_exact(infile.get(), entry_file_offset, &entry, sizeof(entry)) ||
            !decrypt_ctr(entry_offset, reinterpret_cast<uint8_t *>(&entry), sizeof(entry))) {
            LOG_ERROR("Unable to read or decrypt PKG table entry {}", i);
            return false;
        }

        const uint64_t name_offset = byte_swap(entry.name_offset);
        const uint64_t name_size = byte_swap(entry.name_size);
        const uint64_t entry_data_offset = byte_swap(entry.data_offset);
        const uint64_t entry_data_size = byte_swap(entry.data_size);
        if (name_offset % 16 != 0 || entry_data_offset % 16 != 0 ||
            name_size == 0 || name_size > 4096 ||
            !range_within(name_offset, name_size, data_size) ||
            !range_within(entry_data_offset, entry_data_size, data_size)) {
            LOG_ERROR("PKG entry {} has invalid name/data bounds", i);
            return false;
        }
        const uint64_t name_file_offset = data_offset + name_offset;
        const uint64_t data_file_offset = data_offset + entry_data_offset;

        std::vector<uint8_t> name_bytes(static_cast<size_t>(name_size));
        if (!read_exact(infile.get(), name_file_offset, name_bytes.data(), name_bytes.size()) ||
            !decrypt_ctr(name_offset, name_bytes.data(), name_bytes.size())) {
            LOG_ERROR("Unable to read or decrypt PKG entry name {}", i);
            return false;
        }
        while (!name_bytes.empty() && name_bytes.back() == 0)
            name_bytes.pop_back();
        const std::string entry_name(name_bytes.begin(), name_bytes.end());
        const auto relative_path = safe_pkg_entry_path(entry_name);
        if (!relative_path) {
            LOG_ERROR("PKG entry has an unsafe path");
            return false;
        }
        std::string path_identity = relative_path->generic_string();
        std::transform(path_identity.begin(), path_identity.end(), path_identity.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!extracted_paths.insert(path_identity).second) {
            LOG_ERROR("PKG contains duplicate entry path {}", path_identity);
            return false;
        }
        const fs::path destination = staging_path / *relative_path;
        const uint32_t entry_type = byte_swap(entry.type) & 0xFF;
        report_progress(file_count ? static_cast<float>(i) / static_cast<float>(file_count) * 60.f : 0.f);
        if (entry_type == 4 || entry_type == 18) {
            filesystem_error.clear();
            fs::create_directories(destination, filesystem_error);
            if (filesystem_error || !fs::is_directory(destination)) {
                LOG_ERROR("Unable to create PKG directory {}", destination);
                return false;
            }
            continue;
        }

        filesystem_error.clear();
        fs::create_directories(destination.parent_path(), filesystem_error);
        if (filesystem_error || !fs::is_directory(destination.parent_path())) {
            LOG_ERROR("Unable to create PKG file parent {}", destination.parent_path());
            return false;
        }
        fs::ofstream outfile(destination, std::ios::binary | std::ios::trunc);
        if (!outfile) {
            LOG_ERROR("Unable to create PKG output file {}", destination);
            return false;
        }

        uint8_t counter[0x10];
        ctr_init(counter, pkg_header.pkg_data_iv, entry_data_offset / 16);
        if (EVP_DecryptInit_ex(cipher_ctx.get(), cipher_ctr.get(), nullptr, main_key, counter) != 1 ||
            EVP_CIPHER_CTX_set_padding(cipher_ctx.get(), 0) != 1) {
            LOG_ERROR("Unable to initialize PKG data decryption for {}", destination);
            return false;
        }
        uint64_t remaining = entry_data_size;
        uint64_t read_offset = data_file_offset;
        while (remaining != 0) {
            const size_t chunk_size = static_cast<size_t>(std::min<uint64_t>(remaining, buffer.size()));
            if (!read_exact(infile.get(), read_offset, buffer.data(), chunk_size)) {
                LOG_ERROR("Unable to read PKG data for {}", destination);
                return false;
            }
            int decrypted_size = 0;
            if (EVP_DecryptUpdate(cipher_ctx.get(), buffer.data(), &decrypted_size, buffer.data(), static_cast<int>(chunk_size)) != 1 ||
                decrypted_size != static_cast<int>(chunk_size)) {
                LOG_ERROR("Unable to decrypt PKG data for {}", destination);
                return false;
            }
            outfile.write(reinterpret_cast<const char *>(buffer.data()), decrypted_size);
            if (!outfile) {
                LOG_ERROR("Unable to write PKG output file {}", destination);
                return false;
            }
            remaining -= chunk_size;
            read_offset += chunk_size;
        }
        int final_size = 0;
        if (EVP_DecryptFinal_ex(cipher_ctx.get(), buffer.data(), &final_size) != 1) {
            LOG_ERROR("Unable to finalize PKG data decryption for {}", destination);
            return false;
        }
        if (final_size != 0)
            outfile.write(reinterpret_cast<const char *>(buffer.data()), final_size);
        outfile.close();
        if (!outfile) {
            LOG_ERROR("Unable to finish writing PKG output file {}", destination);
            return false;
        }
    }

    report_progress(80);
    std::string zrif = p_zRIF;
    F00DEncryptorTypes f00d_enc_type = F00DEncryptorTypes::native;
    std::string f00d_arg;
    fs::path title_src = staging_path;
    fs::path title_dst = decrypted_path;
    const bool pfs_decrypted = type == PkgType::PKG_TYPE_VITA_THEME
        ? decrypt_theme_pfs(zrif, title_src, title_dst, f00d_enc_type, f00d_arg)
        : execute(zrif, title_src, title_dst, f00d_enc_type, f00d_arg) >= 0;
    if (!pfs_decrypted || !fs::is_directory(decrypted_path)) {
        LOG_ERROR("Unable to decrypt PKG content");
        return false;
    }
    filesystem_error.clear();
    fs::remove_all(staging_path, filesystem_error);
    if (filesystem_error) {
        LOG_ERROR("Unable to replace PKG staging contents after decryption");
        return false;
    }
    fs::rename(decrypted_path, staging_path);
    decrypted_cleanup.release();

    bool installed = false;
    if (type == PkgType::PKG_TYPE_VITA_PATCH) {
        installed = merge_pkg_patch_into_app(emuenv, staging_path);
    } else {
        installed = commit_staged_directory(staging_path, output_path);
        if (installed)
            staging_cleanup.release();
    }
    if (!installed)
        return false;

    if (type != PkgType::PKG_TYPE_VITA_DLC && type != PkgType::PKG_TYPE_VITA_THEME)
        create_license(emuenv, zrif);

    report_progress(100);
    return true;
}

std::string find_pkg_zrif(const fs::path &pkg_path, const fs::path &vita_fs_path) {
    PkgFileHandle infile(FOPEN(pkg_path.c_str(), "rb"), &fclose);
    if (!infile)
        return {};

    boost::system::error_code size_error;
    const uint64_t pkg_size = fs::file_size(pkg_path, size_error);
    if (size_error || pkg_size < sizeof(PkgHeader))
        return {};

    PkgHeader pkg_header{};
    if (!read_exact(infile.get(), 0, &pkg_header, sizeof(pkg_header)) || byte_swap(pkg_header.magic) != 0x7F504b47)
        return {};

    const auto content_id_end = std::find(pkg_header.content_id, pkg_header.content_id + sizeof(pkg_header.content_id), '\0');
    const std::string content_id(pkg_header.content_id, content_id_end);
    if (content_id.size() < 16 || !safe_install_component(content_id))
        return {};

    const std::string title_id = content_id.substr(7, 9);
    if (!safe_install_component(title_id))
        return {};
    const auto rif_path = vita_fs_path / "ux0/license" / title_id / (content_id + ".rif");

    if (!fs::exists(rif_path))
        return {};

    LOG_INFO("Found license file: {}", rif_path);
    fs::ifstream binfile(rif_path, std::ios::in | std::ios::binary | std::ios::ate);
    if (!binfile)
        return {};

    return rif2zrif(binfile);
}
