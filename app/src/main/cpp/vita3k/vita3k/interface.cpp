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

#include "interface.h"
#include "archive.h"

#include "module/load_module.h"

#include <cheat/functions.h>
#include <config/state.h>
#include <ctime>
#include <ctrl/state.h>
#include <dialog/state.h>
#include <display/functions.h>
#include <display/state.h>
#include <emuenv/state.h>
#include <io/functions.h>
#include <io/vfs.h>
#include <kernel/state.h>
#include <lang/state.h>
#include <packages/pkg.h>
#include <packages/sfo.h>
#include <packages/vci.h>
#include <renderer/state.h>
#include <renderer/texture_cache.h>

#include <miniz.h>
#include <pugixml.hpp>

#include <modules/module_parent.h>
#include <string>
#include <util/log.h>
#include <util/string_utils.h>
#include <util/vector_utils.h>
#include <util/vita_theme_utils.h>

#include <unordered_set>

#include <gdbstub/functions.h>
#include <stb_image_write.h>

#if USE_DISCORD
#include <app/discord.h>
#endif

#include "patch/patch.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <regex>

typedef std::shared_ptr<mz_zip_archive> ZipPtr;

inline void delete_zip(mz_zip_archive *zip) {
    mz_zip_reader_end(zip);
    delete zip;
}

static size_t write_to_buffer(void *pOpaque, mz_uint64 file_ofs, const void *pBuf, size_t n) {
    vfs::FileBuffer *const buffer = static_cast<vfs::FileBuffer *>(pOpaque);
    assert(file_ofs == buffer->size());
    const uint8_t *const first = static_cast<const uint8_t *>(pBuf);
    const uint8_t *const last = &first[n];
    buffer->insert(buffer->end(), first, last);

    return n;
}

static const char *miniz_get_error(const ZipPtr &zip) {
    return mz_zip_get_error_string(mz_zip_get_last_error(zip.get()));
}

static std::string fallback_theme_root_name(const std::string &content_path) {
    std::string trimmed = content_path;
    while (!trimmed.empty() && ((trimmed.back() == '/') || (trimmed.back() == '\\')))
        trimmed.pop_back();

    if (trimmed.empty())
        return {};

    const auto separator = trimmed.find_last_of("/\\");
    return (separator == std::string::npos) ? trimmed : trimmed.substr(separator + 1);
}

static std::string normalize_archive_path(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    const bool rooted = !path.empty() && path.front() == '/';
    const bool trailing_separator = !path.empty() && path.back() == '/';
    std::vector<std::string> components;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t separator = path.find('/', start);
        const size_t end = separator == std::string::npos ? path.size() : separator;
        const std::string component = path.substr(start, end - start);
        if (!component.empty() && component != ".")
            components.push_back(component);
        if (separator == std::string::npos)
            break;
        start = separator + 1;
    }

    std::string normalized = rooted ? "/" : "";
    for (const auto &component : components) {
        if (!normalized.empty() && normalized.back() != '/')
            normalized.push_back('/');
        normalized += component;
    }
    if (trailing_separator && !normalized.empty() && normalized.back() != '/')
        normalized.push_back('/');
    return normalized;
}

struct ArchiveContentPath {
    static constexpr mz_uint invalid_index = std::numeric_limits<mz_uint>::max();
    std::string path;
    mz_uint sfo_index = invalid_index;
    mz_uint theme_index = invalid_index;
};

static bool is_safe_archive_relative_path(const std::string &path) {
    if (path.empty())
        return true;

    const fs::path relative = fs_utils::utf8_to_path(path);
    if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory())
        return false;

    for (const auto &component : relative) {
        if (component == "..")
            return false;
    }
    return true;
}

static bool is_safe_install_component(const std::string &component) {
    return !component.empty() && component != "." && component != ".." &&
        component.find_first_of("/\\:") == std::string::npos && component.find('\0') == std::string::npos;
}

static std::optional<std::string> archive_content_root(const std::string &archive_entry, const std::string &metadata_path) {
    const std::string normalized_entry = normalize_archive_path(archive_entry);
    std::string folded_entry = normalized_entry;
    std::string folded_metadata = metadata_path;
    std::transform(folded_entry.begin(), folded_entry.end(), folded_entry.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(folded_metadata.begin(), folded_metadata.end(), folded_metadata.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (folded_entry == folded_metadata)
        return std::string{};

    const std::string suffix = "/" + folded_metadata;
    if (!folded_entry.ends_with(suffix))
        return std::nullopt;

    return normalized_entry.substr(0, normalized_entry.size() - metadata_path.size());
}

static std::string archive_path_identity(std::string path) {
    while (!path.empty() && path.back() == '/')
        path.pop_back();
    std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return path;
}

static bool is_vitamin_marker_path(const std::string &path) {
    const std::string normalized = normalize_archive_path(path);
    const std::string marker = "sce_module/steroid.suprx";
    if (normalized == marker)
        return true;
    return normalized.size() > marker.size() &&
        normalized.compare(normalized.size() - marker.size(), marker.size(), marker) == 0 &&
        normalized[normalized.size() - marker.size() - 1] == '/';
}

struct StagingDirectoryCleanup {
    fs::path path;
    bool active = true;

    ~StagingDirectoryCleanup() {
        if (active) {
            boost::system::error_code error;
            fs::remove_all(path, error);
        }
    }

    void release() {
        active = false;
    }
};

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
        LOG_ERROR("Unable to commit staged install at {}: {}", output_path, error.what());
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
            LOG_WARN("Installed content but could not remove backup {}: {}", backup_path, error.message());
    }
    return true;
}

static bool apply_staged_patch(EmuEnvState &emuenv, const fs::path &patch_path) {
    const fs::path app_path = emuenv.vita_fs_path / "ux0/app" / emuenv.app_info.app_title_id;
    const fs::path app_staging_path = app_path.parent_path() /
        (app_path.filename().string() + ".patch-" + fs::unique_path().string());
    if (!fs::create_directory(app_staging_path)) {
        LOG_ERROR("Unable to create temporary patched app directory {}", app_staging_path);
        return false;
    }
    StagingDirectoryCleanup app_staging_cleanup{ app_staging_path };

    if (!fs_utils::copy_directory_contents(app_path, app_staging_path)) {
        LOG_ERROR("Unable to copy installed app to patch staging directory {}", app_staging_path);
        return false;
    }
    if (!fs_utils::copy_directory_contents(patch_path, app_staging_path)) {
        LOG_ERROR("Unable to merge patch contents into staging directory {}", app_staging_path);
        return false;
    }
    if (!commit_staged_directory(app_staging_path, app_path))
        return false;

    app_staging_cleanup.release();
    return true;
}

static bool is_nonpdrm(EmuEnvState &emuenv, const fs::path &output_path) {
    const auto app_license_path{ emuenv.vita_fs_path / "ux0/license" / emuenv.app_info.app_title_id / fmt::format("{}.rif", emuenv.app_info.app_content_id) };
    const auto is_patch_found_app_license = (emuenv.app_info.app_category == "gp") && fs::exists(app_license_path);
    if (fs::exists(output_path / "sce_sys/package/work.bin") || is_patch_found_app_license) {
        fs::path licpath = is_patch_found_app_license ? app_license_path : output_path / "sce_sys/package/work.bin";
        LOG_INFO("Decrypt layer: {}", output_path);
        if (!decrypt_install_nonpdrm(emuenv, licpath, output_path)) {
            LOG_ERROR("NoNpDrm installation failed, deleting data!");
            fs::remove_all(output_path);
            return false;
        }
        return true;
    }

    return false;
}

static bool set_content_path(EmuEnvState &emuenv, const bool is_theme, fs::path &dest_path) {
    if (!is_safe_install_component(emuenv.app_info.app_title_id)) {
        LOG_ERROR("param.sfo has an unsafe title ID '{}', not installing it", emuenv.app_info.app_title_id);
        return false;
    }

    const auto app_path = dest_path / "app" / emuenv.app_info.app_title_id;

    if (emuenv.app_info.app_category == "ac") {
        if (is_theme) {
            if (!is_safe_install_component(emuenv.app_info.app_content_id)) {
                LOG_ERROR("Content has an unsafe theme ID '{}', not installing it", emuenv.app_info.app_content_id);
                return false;
            }
            dest_path /= fs::path("theme") / emuenv.app_info.app_content_id;
            emuenv.app_info.app_title += " (Theme)";
        } else {
            if (emuenv.app_info.app_content_id.size() <= 20) {
                LOG_ERROR("DLC content ID is too short to contain a content folder");
                return false;
            }
            emuenv.app_info.app_content_id = emuenv.app_info.app_content_id.substr(20);
            if (!is_safe_install_component(emuenv.app_info.app_content_id)) {
                LOG_ERROR("Content has an unsafe DLC ID '{}', not installing it", emuenv.app_info.app_content_id);
                return false;
            }
            dest_path /= fs::path("addcont") / emuenv.app_info.app_title_id / emuenv.app_info.app_content_id;
            emuenv.app_info.app_title += " (DLC)";
        }
    } else if (emuenv.app_info.app_category.contains("gp")) {
        if (!fs::exists(app_path) || fs::is_empty(app_path)) {
            LOG_ERROR("Install app before patch");
            return false;
        }
        dest_path /= fs::path("patch") / emuenv.app_info.app_title_id;
        emuenv.app_info.app_title += " (Patch)";
    } else {
        dest_path = app_path;
        emuenv.app_info.app_title += " (App)";
    }

    return true;
}

static void set_theme_name(EmuEnvState &emuenv, const vfs::FileBuffer &buffer, const std::string &fallback_id_hint = {}) {
    std::string content_id;
    std::string title;

    pugi::xml_document doc;
    if (doc.load_buffer(buffer.data(), buffer.size())) {
        const auto info = doc.child("theme").child("InfomationProperty");
        content_id = info.child("m_contentId").text().as_string();
        title = info.child("m_title").child("m_default").text().as_string();
    } else {
        LOG_WARN("Unable to parse theme.xml metadata during install, falling back to folder/title-derived theme identity");
    }

    const std::string resolved_id = vita_theme_utils::resolve_theme_id(
        content_id,
        fallback_id_hint,
        title,
        buffer.empty() ? nullptr : buffer.data(),
        buffer.size());
    emuenv.app_info.app_content_id = resolved_id;
    emuenv.app_info.app_title_id = resolved_id;

    if (!title.empty()) {
        emuenv.app_info.app_title = title;
    } else if (emuenv.app_info.app_title.empty()) {
        emuenv.app_info.app_title = resolved_id;
    }
}

static bool install_archive_content(
    EmuEnvState &emuenv,
    const ZipPtr &zip,
    const ArchiveContentPath &content,
    const std::vector<ArchiveContentPath> &all_content_paths,
    const std::function<void(ArchiveContents)> &progress_callback,
    const ReinstallCallback &reinstall_callback) {
    // Each entry must supply its own identity, even in a multi-content archive.
    emuenv.app_info = {};
    std::string sfo_path = "sce_sys/param.sfo";
    std::string theme_path = "theme.xml";
    const std::string &normalized_content_path = content.path;
    if (!is_safe_archive_relative_path(normalized_content_path)) {
        LOG_ERROR("Rejecting unsafe archive content prefix '{}'", normalized_content_path);
        return false;
    }
    vfs::FileBuffer buffer, theme;

    LOG_INFO("Installing archive content '{}'", normalized_content_path.empty() ? "<archive root>" : normalized_content_path);

    const auto is_theme = content.theme_index != ArchiveContentPath::invalid_index &&
        mz_zip_reader_extract_to_callback(zip.get(), content.theme_index, &write_to_buffer, &theme, 0);
    const std::string theme_root_name = fallback_theme_root_name(normalized_content_path);

    LOG_INFO("Reading {}{} from archive...", normalized_content_path, sfo_path);
    auto output_path{ emuenv.vita_fs_path / "ux0" };
    if (content.sfo_index != ArchiveContentPath::invalid_index &&
        mz_zip_reader_extract_to_callback(zip.get(), content.sfo_index, &write_to_buffer, &buffer, 0)) {
        LOG_INFO("param.sfo read ({} bytes)", buffer.size());
        if (!sfo::get_param_info(emuenv.app_info, buffer, emuenv.cfg.sys_lang)) {
            LOG_ERROR("Rejecting content '{}': param.sfo failed to parse ({} bytes)", normalized_content_path, buffer.size());
            return false;
        }
        if (!set_content_path(emuenv, is_theme, output_path))
            return false;
    } else if (is_theme) {
        set_theme_name(emuenv, theme, theme_root_name);
        if (!is_safe_install_component(emuenv.app_info.app_title_id) ||
            !is_safe_install_component(emuenv.app_info.app_content_id)) {
            LOG_ERROR("Theme metadata has an unsafe install ID");
            return false;
        }
        output_path /= fs::path("theme") / emuenv.app_info.app_content_id;
    } else {
        LOG_CRITICAL("miniz error: {} extracting file: {}{}", miniz_get_error(zip), normalized_content_path, sfo_path);
        return false;
    }

    const auto created = fs::create_directories(output_path.parent_path());
    if (!created && !fs::is_directory(output_path.parent_path())) {
        LOG_ERROR("Unable to create install parent directory {}", output_path.parent_path());
        return false;
    }

    if (fs::exists(output_path) && reinstall_callback) {
        if (!reinstall_callback(emuenv.app_info.app_title, emuenv.app_info.app_title_id)) {
            LOG_INFO("{} already installed, skipping", emuenv.app_info.app_title_id);
            return true;
        }
    }

    std::vector<std::string> normalized_content_paths;
    normalized_content_paths.reserve(all_content_paths.size());
    std::transform(all_content_paths.begin(), all_content_paths.end(), std::back_inserter(normalized_content_paths), [](const auto &entry) { return entry.path; });

    const fs::path staging_path = output_path.parent_path() /
        (output_path.filename().string() + ".install-" + fs::unique_path().string());
    if (!fs::create_directory(staging_path)) {
        LOG_ERROR("Unable to create temporary install directory {}", staging_path);
        return false;
    }
    StagingDirectoryCleanup staging_cleanup{ staging_path };
    StagingDirectoryCleanup decrypt_staging_cleanup{ fs_utils::path_concat(staging_path, "_dec") };

    float file_progress = 0;
    float decrypt_progress = 0;

    const auto update_progress = [&]() {
        if (progress_callback)
            progress_callback({ {}, {}, { file_progress * 0.7f + decrypt_progress * 0.3f } });
    };

    mz_uint num_files = mz_zip_reader_get_num_files(zip.get());
    LOG_INFO("Extracting {} archive file(s) to {}...", num_files, output_path);
    for (mz_uint i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(zip.get(), i, &file_stat)) {
            continue;
        }
        const std::string m_filename = normalize_archive_path(file_stat.m_filename);
        if (m_filename.starts_with(normalized_content_path)) {
            const bool belongs_to_nested_content = std::any_of(
                normalized_content_paths.begin(), normalized_content_paths.end(), [&](const std::string &other_content_path) {
                    return other_content_path != normalized_content_path &&
                        other_content_path.starts_with(normalized_content_path) &&
                        m_filename.starts_with(other_content_path);
                });
            if (belongs_to_nested_content)
                continue;

            file_progress = static_cast<float>(i) / num_files * 100.0f;
            update_progress();

            std::string replace_filename = m_filename.substr(normalized_content_path.size());
            if (replace_filename.empty())
                continue;
            if (!is_safe_archive_relative_path(replace_filename)) {
                LOG_ERROR("Rejecting unsafe archive entry '{}'", m_filename);
                return false;
            }
            if (i == content.sfo_index)
                replace_filename = "sce_sys/param.sfo";
            else if (i == content.theme_index)
                replace_filename = "theme.xml";

            if (is_vitamin_marker_path(replace_filename)) {
                LOG_WARN("Skipping Vitamin marker during archive extraction: {}", m_filename);
                continue;
            }
            const fs::path file_output = (staging_path / fs_utils::utf8_to_path(replace_filename)).generic_path();
            if (mz_zip_reader_is_file_a_directory(zip.get(), i)) {
                fs::create_directories(file_output);
                if (!fs::is_directory(file_output)) {
                    LOG_ERROR("Unable to create archive directory {}", file_output);
                    return false;
                }
            } else {
                fs::create_directories(file_output.parent_path());
                LOG_INFO("Extracting {}", file_output);
                if (!mz_zip_reader_extract_to_file(zip.get(), i, fs_utils::path_to_utf8(file_output).c_str(), 0)) {
                    LOG_ERROR("miniz error extracting {}: {}", m_filename, miniz_get_error(zip));
                    return false;
                }
            }
        }
    }

    if (fs::exists(staging_path / "sce_sys/package/") && emuenv.app_info.app_title_id.starts_with("PCS")) {
        update_progress();
        if (is_nonpdrm(emuenv, staging_path))
            decrypt_progress = 100.f;
        else
            return false;
    }

    if (emuenv.app_info.app_category.contains("gp")) {
        if (!apply_staged_patch(emuenv, staging_path))
            return false;
    } else {
        if (!commit_staged_directory(staging_path, output_path))
            return false;
        staging_cleanup.release();
        if (!copy_path(output_path, emuenv.vita_fs_path, emuenv.app_info.app_title_id, emuenv.app_info.app_category))
            return false;
    }

    update_progress();

    LOG_INFO("{} [{}] installed successfully!", emuenv.app_info.app_title, emuenv.app_info.app_title_id);

    return true;
}

static std::vector<ArchiveContentPath> get_archive_contents_path(const ZipPtr &zip, bool &valid) {
    mz_uint num_files = mz_zip_reader_get_num_files(zip.get());
    std::vector<ArchiveContentPath> content_paths;
    valid = true;
    const std::string sfo_path = "sce_sys/param.sfo";
    const std::string theme_path = "theme.xml";
    std::unordered_set<std::string> archive_path_identities;
    bool relative_root_directory_seen = false;

    for (mz_uint i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(zip.get(), i, &file_stat)) {
            valid = false;
            return {};
        }

        const std::string m_filename = normalize_archive_path(file_stat.m_filename);
        if (!is_safe_archive_relative_path(m_filename)) {
            LOG_ERROR("Rejecting unsafe archive entry '{}'", file_stat.m_filename);
            valid = false;
            return {};
        }
        const bool is_directory = mz_zip_reader_is_file_a_directory(zip.get(), i);
        if (m_filename.empty()) {
            if (file_stat.m_filename[0] == '\0' || !is_directory || relative_root_directory_seen) {
                LOG_ERROR("Rejecting empty or duplicate relative-root archive entry");
                valid = false;
                return {};
            }
            relative_root_directory_seen = true;
            continue;
        }
        const std::string identity = archive_path_identity(m_filename);
        if (identity.empty() || !archive_path_identities.insert(identity).second) {
            LOG_ERROR("Rejecting duplicate or empty archive entry '{}'", file_stat.m_filename);
            valid = false;
            return {};
        }
        if (is_vitamin_marker_path(m_filename)) {
            LOG_WARN("A Vitamin marker was detected; continuing archive installation.");
        }

        if (is_directory)
            continue;

        auto root = archive_content_root(m_filename, sfo_path);
        const bool is_sfo = root.has_value();
        if (!root)
            root = archive_content_root(m_filename, theme_path);
        if (!root)
            continue;

        auto existing = std::find_if(content_paths.begin(), content_paths.end(), [&](const auto &entry) { return entry.path == *root; });
        if (existing == content_paths.end()) {
            content_paths.push_back({ *root });
            existing = std::prev(content_paths.end());
        }
        if (is_sfo)
            existing->sfo_index = i;
        else
            existing->theme_index = i;
    }

    return content_paths;
}

std::vector<ContentInfo> install_archive(EmuEnvState &emuenv, const fs::path &archive_path, const std::function<void(ArchiveContents)> &progress_callback, const ReinstallCallback &reinstall_callback) {
    if (string_utils::tolower(archive_path.extension().string()) == ".vci") {
        const auto vci_progress = [&](float pct) {
            if (progress_callback)
                progress_callback({ 1.f, 1.f, pct });
        };
        const bool state = install_vci(archive_path, emuenv, vci_progress);
        std::vector<ContentInfo> content_installed{};
        content_installed.push_back({ emuenv.app_info.app_title, emuenv.app_info.app_title_id, emuenv.app_info.app_category, emuenv.app_info.app_content_id, archive_path.string(), state });
        return content_installed;
    }

    FILE *vpk_fp = FOPEN(archive_path.c_str(), "rb");
    if (!vpk_fp) {
        LOG_CRITICAL("Failed to load archive file in path: {}", fs_utils::path_to_utf8(archive_path));
        return {};
    }

    const ZipPtr zip(new mz_zip_archive, delete_zip);
    std::memset(zip.get(), 0, sizeof(*zip));

    if (!mz_zip_reader_init_cfile(zip.get(), vpk_fp, 0, 0)) {
        LOG_CRITICAL("miniz error reading archive: {}", miniz_get_error(zip));
        fclose(vpk_fp);
        return {};
    }

    const mz_uint archive_num_files = mz_zip_reader_get_num_files(zip.get());
    bool valid_archive = false;
    const auto content_path = get_archive_contents_path(zip, valid_archive);
    if (!valid_archive) {
        LOG_ERROR("Rejecting archive with unsafe or ambiguous entry names");
        fclose(vpk_fp);
        return {};
    }
    LOG_INFO("Archive {}: {} file(s), {} content(s) found", fs_utils::path_to_utf8(archive_path.filename()), archive_num_files, content_path.size());
    if (content_path.empty()) {
        for (mz_uint i = 0; i < std::min<mz_uint>(archive_num_files, 8); i++) {
            mz_zip_archive_file_stat file_stat;
            if (mz_zip_reader_file_stat(zip.get(), i, &file_stat))
                LOG_ERROR("No installable content found; archive entry {}: '{}'", i, file_stat.m_filename);
        }
        fclose(vpk_fp);
        return {};
    }

    const auto count = static_cast<float>(content_path.size());
    float current = 0.f;
    const auto update_progress = [&]() {
        if (progress_callback)
            progress_callback({ count, current, {} });
    };
    update_progress();

    std::vector<ContentInfo> content_installed{};
    for (const auto &path : content_path) {
        current++;
        update_progress();
        bool state = install_archive_content(emuenv, zip, path, content_path, progress_callback, reinstall_callback);
        // Can't use emplace_back due to Clang 15 for macos
        content_installed.push_back({ emuenv.app_info.app_title, emuenv.app_info.app_title_id, emuenv.app_info.app_category, emuenv.app_info.app_content_id, path.path, state });
    }

    fclose(vpk_fp);
    return content_installed;
}

static std::vector<fs::path> get_contents_path(const fs::path &path) {
    std::vector<fs::path> contents_path;

    for (const auto &p : fs::recursive_directory_iterator(path)) {
        auto filename = p.path().filename();
        const auto is_content = (filename == "param.sfo") || (filename == "theme.xml");
        if (is_content) {
            auto parent_path = p.path().parent_path();
            const auto content_path = (filename == "param.sfo") ? parent_path.parent_path() : parent_path;
            vector_utils::push_if_not_exists(contents_path, content_path);
        }
    }

    return contents_path;
}

static bool install_content(EmuEnvState &emuenv, const fs::path &content_path) {
    emuenv.app_info = {};
    const auto sfo_path{ content_path / "sce_sys/param.sfo" };
    const auto theme_path{ content_path / "theme.xml" };
    vfs::FileBuffer buffer;

    const auto is_theme = fs::exists(theme_path);
    auto dst_path{ emuenv.vita_fs_path / "ux0" };
    if (fs_utils::read_data(sfo_path, buffer)) {
        if (!sfo::get_param_info(emuenv.app_info, buffer, emuenv.cfg.sys_lang)) {
            LOG_ERROR("Rejecting content '{}': param.sfo failed to parse ({} bytes)", content_path, buffer.size());
            return false;
        }
        if (!set_content_path(emuenv, is_theme, dst_path))
            return false;

        if (exists(dst_path))
            fs::remove_all(dst_path);

    } else if (fs_utils::read_data(theme_path, buffer)) {
        set_theme_name(emuenv, buffer, fs_utils::path_to_utf8(content_path.filename()));
        dst_path /= fs::path("theme") / emuenv.app_info.app_title_id;
    } else {
        LOG_ERROR("Param.sfo file is missing in path", sfo_path);
        return false;
    }

    if (!fs_utils::copy_directory_contents(content_path, dst_path)) {
        LOG_ERROR("Failed to copy directory to: {}", dst_path);
        return false;
    }

    if (fs::exists(dst_path / "sce_sys/package/") && !is_nonpdrm(emuenv, dst_path))
        return false;

    if (!copy_path(dst_path, emuenv.vita_fs_path, emuenv.app_info.app_title_id, emuenv.app_info.app_category))
        return false;

    LOG_INFO("{} [{}] installed successfully!", emuenv.app_info.app_title, emuenv.app_info.app_title_id);

    return true;
}

uint32_t install_contents(EmuEnvState &emuenv, const fs::path &path) {
    const auto src_path = get_contents_path(path);

    LOG_WARN_IF(src_path.empty(), "No found any content compatible on this path: {}", path);

    uint32_t installed = 0;
    for (const auto &src : src_path) {
        if (install_content(emuenv, src))
            ++installed;
    }

    if (installed) {
        LOG_INFO("Successfully installed {} content!", installed);
    }

    return installed;
}

static void do_patches(MemState &mem, const Patches &patches, const SceKernelModuleInfo &sceKernelModuleInfo) {
    for (const auto &patch : patches) {
        if (patch.seg < MODULE_INFO_NUM_SEGMENTS) {
            auto &seg = sceKernelModuleInfo.segments[patch.seg];
            auto seg_ptr = seg.vaddr.cast<uint8_t>();
            if (seg_ptr) {
                LOG_INFO("Patching segment {} at offset 0x{:X} with {} values", patch.seg, patch.offset, patch.values.size());
                if (patch.offset + patch.values.size() <= seg.memsz) {
                    memcpy(seg_ptr.get(mem) + patch.offset, patch.values.data(), patch.values.size());
                } else {
                    LOG_ERROR("Patch out of bounds for segment {} at offset 0x{:X}", patch.seg, patch.offset);
                }
            }
        }
    }
}

// Registers the modules a `$B2` code can pick from along with the cheat file of the title.
static void load_cheats(EmuEnvState &emuenv, const SceKernelModuleInfo &module_info) {
    cheat::unload(emuenv.cheat);

    const cheat::JitInvalidate invalidate_jit = [&emuenv](uint32_t address, size_t size) {
        emuenv.kernel.invalidate_jit_cache(address, size);
    };
    cheat::set_enabled(emuenv.cheat, emuenv.cfg.enable_cheats, emuenv.mem, invalidate_jit);

    // A `$B2` code with module index 0 refers to the main executable.
    CheatModule main_module;
    main_module.name = std::string(module_info.module_name, strnlen(module_info.module_name, sizeof(module_info.module_name)));
    for (size_t i = 0; i < main_module.segments.size(); i++) {
        main_module.segments[i].address = module_info.segments[i].vaddr.address();
        main_module.segments[i].size = module_info.segments[i].memsz;
    }
    cheat::add_module(emuenv.cheat, main_module);

    if (cheat::load(emuenv.cheat, emuenv.cheat_path, emuenv.io.title_id)) {
        LOG_INFO("{} cheats are turned on for {}", cheat::enabled_cheat_count(emuenv.cheat), emuenv.io.title_id);
    }
}

static ExitCode load_app_impl(SceUID &main_module_id, EmuEnvState &emuenv, const AppLaunchRequest &launch_request) {
    const auto call_import = [&emuenv](CPUState &cpu, uint32_t nid, SceUID thread_id) {
        ::call_import(emuenv, cpu, nid, thread_id);
    };
    emuenv.kernel.process_exit_callback = [&emuenv](int res, std::optional<AppLaunchRequest> relaunch) {
        emuenv.post_app_launch_request(relaunch.value_or(AppLaunchRequest{ .reason = AppLaunchReason::ProcessExit }));
    };
    emuenv.kernel.accurate_thread_scheduling = emuenv.cfg.current_config.accurate_thread_scheduling;
    LOG_INFO("CONFIG (applied): memory_mapping={} accurate_thread_scheduling={} high_accuracy={} res_multiplier={} guest_cores={}",
        emuenv.cfg.current_config.memory_mapping, emuenv.cfg.current_config.accurate_thread_scheduling,
        emuenv.cfg.current_config.high_accuracy, emuenv.cfg.current_config.resolution_multiplier, emuenv.cfg.current_config.guest_cores);
    guest_sched_set_cores(emuenv.cfg.current_config.guest_cores);
    if (emuenv.kernel.accurate_thread_scheduling)
        LOG_INFO("Accurate thread scheduling enabled: default-affinity guest threads run one at a time, by priority");

    if (!emuenv.kernel.init(emuenv.mem, call_import, emuenv.cfg.current_config.cpu_opt)) {
        LOG_WARN("Failed to init kernel!");
        return KernelInitFailed;
    }

    if (emuenv.cfg.archive_log) {
        const fs::path log_directory{ emuenv.log_path / "logs" };
        fs::create_directory(log_directory);
        const auto log_path{ log_directory / fs_utils::utf8_to_path(emuenv.io.title_id + " - [" + string_utils::remove_special_chars(emuenv.current_app_title) + "].log") };
        if (logging::add_sink(log_path) != Success)
            return InitConfigFailed;
        logging::set_level(static_cast<spdlog::level::level_enum>(emuenv.cfg.log_level));
    }

    LOG_INFO("CPU Optimisation state: {}", emuenv.cfg.current_config.cpu_opt);
    LOG_INFO("ngs state: {}", emuenv.cfg.current_config.ngs_enable);
    LOG_INFO("Resolution multiplier: {}", emuenv.cfg.resolution_multiplier);

    if (emuenv.ctrl.controllers_num) {
        LOG_INFO("{} Controllers Connected", emuenv.ctrl.controllers_num);
        for (auto controller_it = emuenv.ctrl.controllers.begin(); controller_it != emuenv.ctrl.controllers.end(); ++controller_it) {
            LOG_INFO("Controller {}: {}", controller_it->second.port, controller_it->second.name);
        }
        if (emuenv.ctrl.has_motion_support)
            LOG_INFO("Controller has motion support");
    }
    constexpr std::array modules_mode_names{ "Automatic", "Auto & Manual", "Manual" };
    LOG_INFO("modules mode: {}", modules_mode_names.at(emuenv.cfg.current_config.modules_mode));
    if ((emuenv.cfg.current_config.modules_mode != ModulesMode::AUTOMATIC) && !emuenv.cfg.current_config.lle_modules.empty()) {
        std::string modules;
        for (const auto &mod : emuenv.cfg.current_config.lle_modules) {
            modules += mod + ",";
        }
        modules.pop_back();
        LOG_INFO("lle-modules: {}", modules);
    }

    LOG_INFO("Title: {}", emuenv.current_app_title);
    LOG_INFO("Serial: {}", emuenv.io.title_id);
    LOG_INFO("Version: {}", emuenv.app_info.app_version);
    LOG_INFO("Category: {}", emuenv.app_info.app_category);

    init_device_paths(emuenv.io);
    init_savedata_app_path(emuenv.io, emuenv.vita_fs_path);

    // Load param.sfo
    vfs::FileBuffer param_sfo;
    if (vfs::read_app_file(param_sfo, emuenv.vita_fs_path, emuenv.io.app_path, "sce_sys/param.sfo"))
        sfo::load(emuenv.sfo_handle, param_sfo);

    init_exported_vars(emuenv);

    // Load main executable
    if (!launch_request.self_path.empty()) {
        emuenv.self_path = launch_request.self_path;
    } else {
        emuenv.self_path = !emuenv.cfg.self_path.empty() ? emuenv.cfg.self_path : EBOOT_PATH;
    }

    main_module_id = load_module(emuenv, "app0:" + emuenv.self_path);

    if (main_module_id >= 0) {
        const auto module = emuenv.kernel.loaded_modules[main_module_id];
        LOG_INFO("Main executable {} ({}) loaded", module->info.module_name, emuenv.self_path);
        const Patches patches = get_patches(emuenv.patch_path, emuenv.io.title_id, "app0:" + emuenv.self_path);
        if (!patches.empty())
            do_patches(emuenv.mem, patches, module->info);
        load_cheats(emuenv, module->info);
    } else
        return FileNotFound;
    // Set self name from self path, can contain folder, get file name only
    emuenv.self_name = fs_utils::path_to_utf8(fs::path(emuenv.self_path).filename());

    // get list of preload modules
    SceUInt32 process_preload_disabled = 0;
    auto process_param = emuenv.kernel.process_param.get(emuenv.mem);
    if (process_param) {
        auto preload_disabled_ptr = Ptr<SceUInt32>(process_param->process_preload_disabled);
        if (preload_disabled_ptr) {
            process_preload_disabled = *preload_disabled_ptr.get(emuenv.mem);
        }
    }
    const auto module_app_path{ emuenv.vita_fs_path / "ux0/app" / emuenv.io.app_path / "sce_module" };

    std::vector<std::string> lib_load_list = {};
    // todo: check if module is imported
    auto add_preload_module = [&](uint32_t code, SceSysmoduleModuleId module_id, const std::string &name, bool load_from_app) {
        if ((process_preload_disabled & code) == 0) {
            if (is_lle_module(name, emuenv)) {
                const auto module_name_file = fmt::format("{}.suprx", name);
                if (load_from_app && fs::exists(module_app_path / module_name_file))
                    lib_load_list.emplace_back(fmt::format("app0:sce_module/{}", module_name_file));
                else if (fs::exists(emuenv.vita_fs_path / "vs0/sys/external" / module_name_file))
                    lib_load_list.emplace_back(fmt::format("vs0:sys/external/{}", module_name_file));
            }

            if (module_id != SCE_SYSMODULE_INVALID)
                emuenv.kernel.loaded_sysmodules[module_id] = {};
        }
    };
    lib_load_list.emplace_back("os0:kd/bootimage.skprx");
    lib_load_list.emplace_back("os0:kd/sysmodule.skprx");
    add_preload_module(0x00010000, SCE_SYSMODULE_INVALID, "libc", true);
    add_preload_module(0x00020000, SCE_SYSMODULE_DBG, "libdbg", false);
    add_preload_module(0x00080000, SCE_SYSMODULE_INVALID, "libshellsvc", false);
    add_preload_module(0x00100000, SCE_SYSMODULE_INVALID, "libcdlg", false);
    add_preload_module(0x00200000, SCE_SYSMODULE_FIOS2, "libfios2", true);
    add_preload_module(0x00400000, SCE_SYSMODULE_APPUTIL, "apputil", false);
    add_preload_module(0x00800000, SCE_SYSMODULE_INVALID, "libSceFt2", false);
    add_preload_module(0x01000000, SCE_SYSMODULE_INVALID, "libpvf", false);
    add_preload_module(0x02000000, SCE_SYSMODULE_PERF, "libperf", false); // if DEVELOPMENT_MODE dipsw is set

    for (const auto &module_path : lib_load_list) {
        auto res = load_module(emuenv, module_path);
        LOG_ERROR_IF(res < 0, "Failed to load preloaded module: {}. Ignoring this error.", module_path);
    }

    // Load taiHEN plugins configured for this title
    load_taihen_plugins_for_title(emuenv, emuenv.io.title_id);

    return Success;
}

void toggle_texture_replacement(EmuEnvState &emuenv) {
    emuenv.cfg.current_config.import_textures = !emuenv.cfg.current_config.import_textures;
    emuenv.renderer->get_texture_cache()->set_replacement_state(emuenv.cfg.current_config.import_textures, emuenv.cfg.current_config.export_textures, emuenv.cfg.current_config.export_as_png);
}

static std::vector<uint32_t> get_current_app_frame(EmuEnvState &emuenv, uint32_t &width, uint32_t &height) {
    // Dump the current frame from the emulator display
    std::vector<uint32_t> frame = emuenv.renderer->dump_frame(emuenv.display, width, height);
    if (frame.empty() || (frame.size() != (width * height))) {
        return {};
    }

    // Force alpha channel to 255 (fully opaque) for every pixel
    for (uint32_t &pixel : frame) {
        pixel |= 0xFF000000;
    }

    return frame;
}

void take_screenshot(EmuEnvState &emuenv) {
    if (emuenv.cfg.screenshot_format == None)
        return;

    if (emuenv.io.title_id.empty()) {
        LOG_ERROR("Trying to take a screenshot while not ingame");
        return;
    }

    uint32_t width, height;
    auto frame = get_current_app_frame(emuenv, width, height);
    if (frame.empty()) {
        LOG_ERROR("Failed to take screenshot");
        return;
    }

    const fs::path save_folder = emuenv.shared_path / "screenshots" / fmt::format("{}", string_utils::remove_special_chars(emuenv.current_app_title));
    fs::create_directories(save_folder);

    auto t = std::time(nullptr);
    struct tm localtime;
#ifdef _WIN32
    localtime_s(&localtime, &t);
#else
    localtime_r(&t, &localtime);
#endif

    const auto img_format = emuenv.cfg.screenshot_format == JPEG ? ".jpg" : ".png";
    const fs::path save_file = save_folder / fmt::format("{}_{:%Y-%m-%d-%H%M%OS}{}", string_utils::remove_special_chars(emuenv.current_app_title), localtime, img_format);
    constexpr int quality = 85; // google recommended value
    if (emuenv.cfg.screenshot_format == JPEG) {
        if (stbi_write_jpg(fs_utils::path_to_utf8(save_file).c_str(), width, height, 4, frame.data(), quality) == 1)
            LOG_INFO("Successfully saved screenshot to {}", save_file);
        else
            LOG_INFO("Failed to save screenshot");
    } else {
        if (stbi_write_png(fs_utils::path_to_utf8(save_file).c_str(), width, height, 4, frame.data(), width * 4) == 1)
            LOG_INFO("Successfully saved screenshot to {}", save_file);
        else
            LOG_INFO("Failed to save screenshot");
    }
}

ExitCode load_app(int32_t &main_module_id, EmuEnvState &emuenv) {
    return load_app(main_module_id, emuenv, AppLaunchRequest{
                                                .app_path = emuenv.io.app_path,
                                            });
}

ExitCode load_app(int32_t &main_module_id, EmuEnvState &emuenv, const AppLaunchRequest &launch_request) {
    if (load_app_impl(main_module_id, emuenv, launch_request) != Success) {
        std::string message = fmt::format(fmt::runtime(lang::get(lang::str::load_app_failed_msg)), emuenv.vita_fs_path / "ux0/app" / emuenv.io.app_path / emuenv.self_path);
        LOG_ERROR(message);
        return ModuleLoadFailed;
    }

    if (emuenv.cfg.gdbstub) {
        emuenv.kernel.debugger.wait_for_debugger = true;
        server_open(emuenv);
    }

#if USE_DISCORD
    if (emuenv.cfg.discord_rich_presence)
        discordrpc::update_presence(emuenv.io.title_id, emuenv.current_app_title);
#endif

    return Success;
}

static std::vector<std::string> split(const std::string &input, const std::string &regex) {
    std::regex re(regex);
    std::sregex_token_iterator
        first{ input.begin(), input.end(), re, -1 },
        last;
    return { first, last };
}

ExitCode run_app(EmuEnvState &emuenv, int32_t main_module_id) {
    return run_app(emuenv, main_module_id, AppLaunchRequest{
                                               .app_path = emuenv.io.app_path,
                                           });
}

ExitCode run_app(EmuEnvState &emuenv, int32_t main_module_id, const AppLaunchRequest &launch_request) {
    auto entry_point = emuenv.kernel.loaded_modules[main_module_id]->info.start_entry;
    auto process_param = emuenv.kernel.process_param.get(emuenv.mem);

    SceInt32 priority = SCE_KERNEL_DEFAULT_PRIORITY_USER;
    SceInt32 stack_size = SCE_KERNEL_STACK_SIZE_USER_MAIN;
    SceInt32 affinity = SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT;
    if (process_param) {
        auto priority_ptr = Ptr<int32_t>(process_param->main_thread_priority);
        if (priority_ptr) {
            priority = *priority_ptr.get(emuenv.mem);
        }

        auto stack_size_ptr = Ptr<int32_t>(process_param->main_thread_stacksize);
        if (stack_size_ptr) {
            stack_size = *stack_size_ptr.get(emuenv.mem);
        }

        auto affinity_ptr = Ptr<SceInt32>(process_param->main_thread_cpu_affinity_mask);
        if (affinity_ptr) {
            affinity = *affinity_ptr.get(emuenv.mem);
        }
    }
    const ThreadStatePtr main_thread = emuenv.kernel.create_thread(emuenv.mem, emuenv.io.title_id.c_str(), entry_point, priority, affinity, stack_size, nullptr);
    if (!main_thread) {
        LOG_ERROR("Failed to init main thread.");
        return InitThreadFailed;
    }
    emuenv.main_thread_id = main_thread->id;

    // Run `module_start` export (entry point) of loaded libraries
    for (auto &[_, module] : emuenv.kernel.loaded_modules) {
        if (module->info.modid != main_module_id)
            start_module(emuenv, module->info);
    }

    SceKernelThreadOptParam param{ 0, 0 };
    std::vector<std::string> cfg_args;
    const auto *args = &launch_request.argv;
    if (args->empty() && !emuenv.cfg.app_args.empty()) {
        cfg_args = split(emuenv.cfg.app_args, ",\\s+");
        args = &cfg_args;
    }
    if (!args->empty()) {
        // why is this flipped
        std::vector<uint8_t> buf;
        for (const auto &arg : *args)
            buf.insert(buf.end(), arg.c_str(), arg.c_str() + arg.size() + 1);
        auto arr = Ptr<uint8_t>(alloc(emuenv.mem, static_cast<uint32_t>(buf.size()), "arg"));
        memcpy(arr.get(emuenv.mem), buf.data(), buf.size());
        param.size = static_cast<SceSize>(buf.size());
        param.attr = arr.address();
    }
    if (main_thread->start(param.size, Ptr<void>(param.attr), true) < 0) {
        LOG_ERROR("Failed to run main thread.");
        return RunThreadFailed;
    }

    start_sync_thread(emuenv);

    return Success;
}
