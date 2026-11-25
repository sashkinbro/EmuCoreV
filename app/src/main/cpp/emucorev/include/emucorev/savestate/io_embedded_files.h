// Guest files that were open when a state was saved, carried inside the state (engine 19+).
//
// A descriptor is restored by reopening its host file. Game data (ux0:/app, patches) is part of
// the install and is required to exist, but user data such as savedata is created by playing, so a
// state restored on another device, or after the savedata was deleted, finds it missing. The state
// therefore embeds the content of small user-data files that are open, and the loader recreates a
// file only when it is absent. An existing host file is never overwritten: it may hold newer
// progress than the state.
//
// IO section layout: [existing tables][optional extension]. The extension is absent in states
// saved before engine 19 and when no embeddable file is open:
//   u32 magic 'IOFC', u32 version (1), u32 count,
//   count x { str path, u32 size, size bytes }
#pragma once

#include <emucorev/savestate/state_io.h>

#include <io/device.h>
#include <io/functions.h>
#include <io/state.h>
#include <util/fs.h>
#include <util/log.h>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace emucorev::savestate::io_files {

constexpr uint32_t kExtensionMagic = 0x43464F49; // "IOFC"
constexpr uint32_t kExtensionVersion = 1;
constexpr uint32_t kMaxFiles = 64;
constexpr uint64_t kMaxFileBytes = 16ull << 20;
constexpr uint64_t kMaxTotalBytes = 48ull << 20;

struct EmbeddedFile {
    std::string path; // normalized guest path, exactly as stored in the descriptor table
    std::vector<uint8_t> data;
};

// Only files a game writes while it runs are embedded. Installed content (ux0:/app, ux0:/patch,
// ux0:/addcont, vs0:, ...) must exist on the host and is never carried.
inline bool embeddable_path(const std::string &path) {
    static const char *const roots[] = { "ux0:/user/", "ux0:/data/", "ux0:/temp/", "ur0:/", "uma0:/" };
    for (const char *root : roots)
        if (path.compare(0, std::char_traits<char>::length(root), root) == 0)
            return true;
    return false;
}

// Reads a whole host file if it is a regular file within kMaxFileBytes.
inline bool read_host_file(const fs::path &path, std::vector<uint8_t> &out) {
    out.clear();
    boost::system::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec)
        return false;
    const uintmax_t size = fs::file_size(path, ec);
    if (ec || size > kMaxFileBytes)
        return false;
    FILE *file = FOPEN(path.c_str(), "rb");
    if (!file)
        return false;
    out.resize(static_cast<size_t>(size));
    const bool ok = out.empty() || std::fread(out.data(), 1, out.size(), file) == out.size();
    std::fclose(file);
    if (!ok)
        out.clear();
    return ok;
}

// Appends the extension; nothing is written for an empty list so such states keep the old layout.
inline void write_extension(BufferWriter &buffer, const std::vector<EmbeddedFile> &files) {
    if (files.empty())
        return;
    buffer.u32(kExtensionMagic);
    buffer.u32(kExtensionVersion);
    buffer.u32(static_cast<uint32_t>(files.size()));
    for (const EmbeddedFile &file : files) {
        buffer.str(file.path);
        buffer.u32(static_cast<uint32_t>(file.data.size()));
        buffer.append(file.data.data(), file.data.size());
    }
}

// Parses the bytes following the descriptor tables. `read_path` reads and validates one guest path
// the same way descriptor paths are validated. An empty remainder is a state without the extension.
inline bool parse_extension(BufferReader &reader, const std::function<bool(BufferReader &, std::string &)> &read_path,
    std::vector<EmbeddedFile> &files, std::string &error) {
    files.clear();
    if (reader.ok() && reader.remaining() == 0)
        return true;
    const auto invalid = [&] { error = "invalid IO embedded file table"; files.clear(); return false; };
    if (reader.u32() != kExtensionMagic || reader.u32() != kExtensionVersion)
        return invalid();
    const uint32_t count = reader.u32();
    if (!reader.ok() || count == 0 || count > kMaxFiles)
        return invalid();
    std::set<std::string> seen;
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; ++i) {
        EmbeddedFile file;
        if (!read_path(reader, file.path) || !embeddable_path(file.path) || !seen.insert(file.path).second)
            return invalid();
        const uint32_t size = reader.u32();
        total += size;
        if (!reader.ok() || size > kMaxFileBytes || total > kMaxTotalBytes || size > reader.remaining())
            return invalid();
        file.data.resize(size);
        if (!reader.bytes(file.data.data(), size))
            return invalid();
        files.push_back(std::move(file));
    }
    if (!reader.ok() || reader.remaining() != 0)
        return invalid();
    return true;
}

inline const EmbeddedFile *find_embedded(const std::vector<EmbeddedFile> &files, const std::string &path) {
    for (const EmbeddedFile &file : files)
        if (file.path == path)
            return &file;
    return nullptr;
}

// Writes `data` to a host file that does not exist yet (parents are created). Never replaces an
// existing file. The content goes to a temporary name first so a crash cannot leave a short file.
inline bool create_missing_host_file(const fs::path &path, const std::vector<uint8_t> &data) {
    boost::system::error_code ec;
    if (fs::exists(path, ec))
        return false;
    fs::create_directories(path.parent_path(), ec);
    fs::path temporary = path;
    temporary += ".ecvs-tmp";
    FILE *file = FOPEN(temporary.c_str(), "wb");
    if (!file)
        return false;
    const bool wrote = data.empty() || std::fwrite(data.data(), 1, data.size(), file) == data.size();
    const bool closed = std::fclose(file) == 0;
    if (!wrote || !closed) {
        fs::remove(temporary, ec);
        return false;
    }
    fs::rename(temporary, path, ec);
    if (ec) {
        fs::remove(temporary, ec);
        return false;
    }
    return true;
}

// Reopens one saved descriptor on `staged` at number `fd`. When the host file is missing and the
// state embeds its content, the file is recreated first. Returns the descriptor, or a value != fd
// on failure (the caller reports it). Descriptor-table swapping stays with the caller.
inline SceUID reopen_saved_file(IOState &staged, const fs::path &vita_fs_path, const std::vector<EmbeddedFile> &embedded,
    const std::string &path, SceUID fd, int32_t mode) {
    staged.next_fd = fd;
    const SceUID opened = open_file(staged, path.c_str(), mode, vita_fs_path, "savestate");
    if (opened == fd)
        return opened;
    const EmbeddedFile *content = find_embedded(embedded, path);
    VitaIoDevice device = device::get_device(path);
    if (!content || device == VitaIoDevice::_INVALID)
        return opened;
    const std::string translated = translate_path(path.c_str(), device, staged.device_paths);
    if (translated.empty()
        || !create_missing_host_file(device::construct_emulated_path(device, translated, vita_fs_path, staged.redirect_stdio), content->data))
        return opened;
    LOG_WARN("[savestate] load: recreated missing open file {} ({} bytes) from the state", path, content->data.size());
    staged.next_fd = fd;
    return open_file(staged, path.c_str(), mode, vita_fs_path, "savestate");
}

// Same for an open directory. Savedata folders only exist once the game has saved, so a missing
// user-data directory is recreated empty rather than failing the load.
inline SceUID reopen_saved_dir(IOState &staged, const fs::path &vita_fs_path, const std::string &path, SceUID fd) {
    staged.next_fd = fd;
    const SceUID opened = open_dir(staged, path.c_str(), vita_fs_path, "savestate");
    if (opened == fd || !embeddable_path(path))
        return opened;
    VitaIoDevice device = device::get_device(path);
    if (device == VitaIoDevice::_INVALID)
        return opened;
    const std::string translated = translate_path(path.c_str(), device, staged.device_paths);
    boost::system::error_code ec;
    if (translated.empty()
        || !fs::create_directories(device::construct_emulated_path(device, translated, vita_fs_path, staged.redirect_stdio), ec))
        return opened;
    LOG_WARN("[savestate] load: recreated missing open directory {}", path);
    staged.next_fd = fd;
    return open_dir(staged, path.c_str(), vita_fs_path, "savestate");
}

} // namespace emucorev::savestate::io_files
