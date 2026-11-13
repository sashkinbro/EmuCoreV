#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace emucorev::archive_expansion {

enum class ContainerFormat { zip, rar4, rar5, seven_zip, unsupported };
enum class Failure {
    none,
    unsupported_format,
    malformed_archive,
    encrypted_archive,
    unsafe_path,
    duplicate_entry,
    link_or_special_entry,
    entry_count_limit,
    expanded_bytes_limit,
    decoder_memory_limit,
    insufficient_space,
    cancelled,
    io_error
};

struct Limits {
    std::uint32_t max_entries = 50000;
    std::uint64_t max_expanded_bytes = 32ull * 1024 * 1024 * 1024;
    std::uint64_t free_space_reserve_bytes = 256ull * 1024 * 1024;
    std::uint64_t max_decoder_memory_bytes = 256ull * 1024 * 1024;
};

struct Progress {
    std::string entry_path;
    std::uint64_t entry_bytes = 0;
    std::uint64_t total_bytes = 0;
};

struct Result {
    Failure failure = Failure::none;
    ContainerFormat format = ContainerFormat::unsupported;
    std::string message;
    explicit operator bool() const { return failure == Failure::none; }
};

using Cancelled = std::function<bool()>;
using ProgressCallback = std::function<void(const Progress&)>;

ContainerFormat detect_format(const std::filesystem::path& input);
Result extract_archive(const std::filesystem::path& input,
                       const std::filesystem::path& private_staging_root,
                       const Limits& limits = {},
                       const Cancelled& cancelled = {},
                       const ProgressCallback& progress = {});

} // namespace emucorev::archive_expansion
