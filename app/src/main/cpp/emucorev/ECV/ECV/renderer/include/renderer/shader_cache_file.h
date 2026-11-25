// On-disk shader cache: identity, integrity and atomic writes (docs/shader-cache.md).
// Two kinds of file live in <cache>/shaders/<title>/<self>/:
//  - the manifest (hashs-<backend>.dat): the list of shader variants to pre-warm at game start;
//  - one entry per compiled SPIR-V module (vk<version>-<variant hash>.spv).
// Both start with a header that carries the identity of whatever produced the content (translator sources,
// translator version, shader-affecting config) plus the payload size and an XXH3 hash, so a file written by
// another translator, a truncated file or a corrupt file is never handed to the driver: it is discarded and
// the shader is recompiled. Pure logic, no emulator dependencies, host-tested
// (app/src/test/core-host/shader_cache_file_tests.cpp).
#pragma once

#include <xxhash.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace renderer {

inline constexpr uint32_t shader_cache_entry_magic = 0x53435632; // "SCV2": framed SPIR-V entry
inline constexpr uint32_t shader_cache_manifest_magic = 0x53434D32; // "SCM2": framed manifest
inline constexpr uint32_t spirv_magic_word = 0x07230203;

// Everything that decides what SPIR-V a program is translated to, other than the program binary and the
// hints (those are part of the variant hash that names the entry).
struct ShaderCacheIdentity {
    uint64_t translator_hash = 0; // build-time hash of the translator sources (shader::translator_source_hash())
    uint32_t translator_version = 0; // shader::CURRENT_VERSION
    uint32_t features_mask = 0; // State::get_features_mask(): force-full-precision, no-fp-contraction, device features, ...

    bool operator==(const ShaderCacheIdentity &) const = default;
};

// 64-bit fingerprint of an identity, stored in every entry.
inline uint64_t shader_cache_identity_key(const ShaderCacheIdentity &id) {
    const uint64_t words[3] = { id.translator_hash, id.translator_version, id.features_mask };
    return XXH3_64bits(words, sizeof(words));
}

enum class ShaderCacheReject : uint8_t {
    None,
    TooSmall,
    Format, // other magic / header size (older build wrote it, or not a cache file)
    Translator, // translator sources changed
    TranslatorVersion, // shader::CURRENT_VERSION changed
    Config, // shader-affecting configuration (features mask) changed
    Identity, // entry written under another identity (cannot tell which part)
    Truncated, // the file is shorter (or longer) than its header says
    Corrupt, // payload hash mismatch or not SPIR-V
};

inline const char *to_string(ShaderCacheReject r) {
    switch (r) {
    case ShaderCacheReject::None: return "accepted";
    case ShaderCacheReject::TooSmall: return "file too small";
    case ShaderCacheReject::Format: return "other file format";
    case ShaderCacheReject::Translator: return "shader translator changed";
    case ShaderCacheReject::TranslatorVersion: return "shader translator version changed";
    case ShaderCacheReject::Config: return "shader config changed";
    case ShaderCacheReject::Identity: return "written by another translator or config";
    case ShaderCacheReject::Truncated: return "file truncated";
    case ShaderCacheReject::Corrupt: return "payload hash mismatch";
    }
    return "?";
}

// ---------------------------------------------------------------------------------------------------------
// SPIR-V entry

struct ShaderCacheEntryHeader {
    uint32_t magic = shader_cache_entry_magic;
    uint32_t header_size = sizeof(uint32_t) * 2 + sizeof(uint64_t) * 3;
    uint64_t identity_key = 0;
    uint64_t payload_size = 0; // bytes of SPIR-V that follow
    uint64_t payload_hash = 0; // XXH3_64 of those bytes
};
static_assert(sizeof(ShaderCacheEntryHeader) == 32, "entry header must have no padding");

inline std::vector<uint8_t> encode_shader_cache_entry(uint64_t identity_key, const uint32_t *words, size_t word_count) {
    ShaderCacheEntryHeader h;
    h.identity_key = identity_key;
    h.payload_size = static_cast<uint64_t>(word_count) * sizeof(uint32_t);
    h.payload_hash = XXH3_64bits(words, h.payload_size);
    std::vector<uint8_t> out(sizeof(h) + h.payload_size);
    std::memcpy(out.data(), &h, sizeof(h));
    if (h.payload_size)
        std::memcpy(out.data() + sizeof(h), words, h.payload_size);
    return out;
}

// Validates a whole entry file already read into memory and extracts the SPIR-V words.
inline ShaderCacheReject decode_shader_cache_entry(const uint8_t *file, size_t file_size, uint64_t expected_key, std::vector<uint32_t> &words) {
    words.clear();
    if (file_size < sizeof(ShaderCacheEntryHeader))
        return ShaderCacheReject::TooSmall;
    ShaderCacheEntryHeader h;
    std::memcpy(&h, file, sizeof(h));
    if (h.magic != shader_cache_entry_magic || h.header_size != sizeof(ShaderCacheEntryHeader))
        return ShaderCacheReject::Format;
    if (h.identity_key != expected_key)
        return ShaderCacheReject::Identity;
    // exact size: a longer file is as suspicious as a shorter one (payload_size comes from the file, no arithmetic on it)
    const uint64_t room = file_size - sizeof(ShaderCacheEntryHeader);
    if (h.payload_size != room || h.payload_size == 0 || h.payload_size % sizeof(uint32_t) != 0)
        return ShaderCacheReject::Truncated;
    if (XXH3_64bits(file + sizeof(h), h.payload_size) != h.payload_hash)
        return ShaderCacheReject::Corrupt;
    std::vector<uint32_t> out(h.payload_size / sizeof(uint32_t));
    std::memcpy(out.data(), file + sizeof(h), h.payload_size);
    if (out[0] != spirv_magic_word)
        return ShaderCacheReject::Corrupt;
    words = std::move(out);
    return ShaderCacheReject::None;
}

// ---------------------------------------------------------------------------------------------------------
// Manifest

struct ShaderCacheManifestHeader {
    uint32_t magic = shader_cache_manifest_magic;
    uint32_t header_size = 0; // set by encode
    ShaderCacheIdentity identity; // 16 bytes
    uint64_t entry_count = 0; // number of ShaderCacheManifestEntry that follow
    uint64_t payload_hash = 0; // XXH3_64 of the entries
};
static_assert(sizeof(ShaderCacheManifestHeader) == 40, "manifest header must have no padding");

struct ShaderCacheManifestEntry {
    std::array<uint8_t, 32> frag{};
    std::array<uint8_t, 32> vert{};
};
static_assert(sizeof(ShaderCacheManifestEntry) == 64, "manifest entry must have no padding");

inline std::vector<uint8_t> encode_shader_cache_manifest(const ShaderCacheIdentity &id, const std::vector<ShaderCacheManifestEntry> &entries) {
    ShaderCacheManifestHeader h;
    h.header_size = sizeof(h);
    h.identity = id;
    h.entry_count = entries.size();
    const size_t payload = entries.size() * sizeof(ShaderCacheManifestEntry);
    h.payload_hash = XXH3_64bits(entries.data(), payload);
    std::vector<uint8_t> out(sizeof(h) + payload);
    std::memcpy(out.data(), &h, sizeof(h));
    if (payload)
        std::memcpy(out.data() + sizeof(h), entries.data(), payload);
    return out;
}

inline ShaderCacheReject decode_shader_cache_manifest(const uint8_t *file, size_t file_size, const ShaderCacheIdentity &expected, std::vector<ShaderCacheManifestEntry> &entries) {
    entries.clear();
    if (file_size < sizeof(ShaderCacheManifestHeader))
        return ShaderCacheReject::TooSmall;
    ShaderCacheManifestHeader h;
    std::memcpy(&h, file, sizeof(h));
    if (h.magic != shader_cache_manifest_magic || h.header_size != sizeof(ShaderCacheManifestHeader))
        return ShaderCacheReject::Format;
    // order = what a developer wants to read first in the log
    if (h.identity.translator_hash != expected.translator_hash)
        return ShaderCacheReject::Translator;
    if (h.identity.translator_version != expected.translator_version)
        return ShaderCacheReject::TranslatorVersion;
    if (h.identity.features_mask != expected.features_mask)
        return ShaderCacheReject::Config;
    const uint64_t room = file_size - sizeof(ShaderCacheManifestHeader);
    if (h.entry_count > room / sizeof(ShaderCacheManifestEntry) || h.entry_count * sizeof(ShaderCacheManifestEntry) != room)
        return ShaderCacheReject::Truncated;
    if (XXH3_64bits(file + sizeof(h), room) != h.payload_hash)
        return ShaderCacheReject::Corrupt;
    entries.resize(h.entry_count);
    if (room)
        std::memcpy(entries.data(), file + sizeof(h), room);
    return ShaderCacheReject::None;
}

// ---------------------------------------------------------------------------------------------------------
// Files

inline bool read_file_bytes(const std::filesystem::path &path, std::vector<uint8_t> &out) {
    out.clear();
    std::ifstream is(path, std::ios::binary | std::ios::ate);
    if (!is)
        return false;
    const std::streamoff size = is.tellg();
    if (size < 0)
        return false;
    is.seekg(0);
    out.resize(static_cast<size_t>(size));
    if (size > 0)
        is.read(reinterpret_cast<char *>(out.data()), size);
    return is.gcount() == size;
}

// Suffix of the temporary file of an in-flight write. Anything with this in its name is garbage after a crash.
inline constexpr const char *shader_cache_temp_marker = ".tmp-";

// Writes to <path>.tmp-<unique> and renames it over <path>, so a reader (or a crash, a full disk, a kill) can only
// ever see the old complete file or the new complete file. The payload hash in the header catches the one case a
// rename cannot (a torn file after power loss on a file system without ordered data).
inline bool write_file_atomic(const std::filesystem::path &path, const void *data, size_t size) {
    static std::atomic<uint32_t> counter{ 0 };
    const size_t tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
    std::filesystem::path tmp = path;
    tmp += shader_cache_temp_marker + std::to_string(tid) + "-" + std::to_string(counter.fetch_add(1, std::memory_order_relaxed));

    std::error_code ec;
    {
        std::ofstream os(tmp, std::ios::binary | std::ios::trunc);
        if (!os)
            return false;
        if (size)
            os.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
        os.flush();
        if (!os) {
            os.close();
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }
    std::filesystem::rename(tmp, path, ec); // replaces an existing file atomically on POSIX and (MoveFileEx) on Windows
    if (ec) {
        std::error_code ignore;
        std::filesystem::remove(tmp, ignore);
        return false;
    }
    return true;
}

inline bool write_file_atomic(const std::filesystem::path &path, const std::vector<uint8_t> &bytes) {
    return write_file_atomic(path, bytes.data(), bytes.size());
}

// Deletes the leftovers of writes that never finished (crash between create and rename). Returns how many.
inline size_t remove_stale_shader_cache_temp_files(const std::filesystem::path &dir) {
    size_t removed = 0;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.find(shader_cache_temp_marker) != std::string::npos) {
            std::error_code rm_ec;
            if (std::filesystem::remove(it->path(), rm_ec))
                ++removed;
        }
    }
    return removed;
}


// ---------------------------------------------------------------------------------------------------------
// Crash-loop guard for the start-of-game pre-warm: a marker is written before the warm-up and removed after it.
// If the previous warm-ups died (marker still there, count >= max) the warm-up is skipped; shaders are then loaded
// lazily as before. The marker lives in the cache directory, so discarding the cache (translator change) re-enables it.
inline constexpr uint32_t shader_cache_max_unclean_warmups = 2;
inline constexpr const char *shader_cache_warmup_marker_name = "warmup.marker";

inline uint32_t read_warmup_marker(const std::filesystem::path &marker) {
    std::vector<uint8_t> bytes;
    if (!read_file_bytes(marker, bytes) || bytes.size() != sizeof(uint32_t) * 2)
        return 0;
    uint32_t v[2];
    std::memcpy(v, bytes.data(), sizeof(v));
    return v[1] == (v[0] ^ 0xC0DE5CA5u) ? v[0] : 0; // checked count; garbage counts as a clean state
}

// Returns false when the warm-up must be skipped. Otherwise the marker is (re)written with the count + 1.
inline bool begin_shader_cache_warmup(const std::filesystem::path &marker, uint32_t *unclean_runs = nullptr) {
    const uint32_t previous = read_warmup_marker(marker);
    if (unclean_runs)
        *unclean_runs = previous;
    if (previous >= shader_cache_max_unclean_warmups)
        return false;
    const uint32_t v[2] = { previous + 1, (previous + 1) ^ 0xC0DE5CA5u };
    write_file_atomic(marker, v, sizeof(v));
    return true;
}

inline void end_shader_cache_warmup(const std::filesystem::path &marker) {
    std::error_code ec;
    std::filesystem::remove(marker, ec);
}

// ---------------------------------------------------------------------------------------------------------
// Background writer: cache files are written by one low-priority thread so neither the render thread nor a
// pipeline compile worker waits for the file system. flush() waits until everything queued so far is on disk
// (call it before wiping the directory and when the game closes).
class ShaderCacheWriter {
public:
    ~ShaderCacheWriter() {
        flush();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        if (thread_.joinable())
            thread_.join();
    }

    void enqueue(std::filesystem::path path, std::vector<uint8_t> bytes) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!thread_.joinable())
                thread_ = std::thread(&ShaderCacheWriter::run, this);
            queue_.push_back({ std::move(path), std::move(bytes) });
            ++pending_;
        }
        cv_.notify_all();
    }

    void flush() {
        std::unique_lock<std::mutex> lock(mutex_);
        idle_cv_.wait(lock, [this] { return pending_ == 0; });
    }

    uint32_t failed_writes() const { return failed_.load(); }

private:
    struct Job {
        std::filesystem::path path;
        std::vector<uint8_t> bytes;
    };

    void run() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (true) {
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (queue_.empty())
                return; // stop_ and drained
            Job job = std::move(queue_.front());
            queue_.pop_front();
            lock.unlock();
            if (!write_file_atomic(job.path, job.bytes))
                ++failed_;
            lock.lock();
            --pending_;
            idle_cv_.notify_all();
        }
    }

    std::mutex mutex_;
    std::condition_variable cv_, idle_cv_;
    std::deque<Job> queue_;
    size_t pending_ = 0;
    bool stop_ = false;
    std::atomic<uint32_t> failed_{ 0 };
    std::thread thread_;
};

} // namespace renderer
