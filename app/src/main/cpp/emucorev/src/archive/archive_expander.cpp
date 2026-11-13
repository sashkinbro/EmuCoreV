#include "emucorev/archive/archive_expander.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <memory>
#include <set>
#include <system_error>
#include <vector>

namespace emucorev::archive_expansion {
namespace {

constexpr std::size_t kCopyBufferBytes = 32 * 1024;
// Compile-time decoder patch bounds: RAR5 uses at most two 32 MiB windows;
// 7z limits its dictionary and each BCJ2 substream to 32 MiB.
constexpr std::uint64_t kDecoderPeakCapBytes = 128ull * 1024 * 1024;

bool starts_with(const std::array<unsigned char, 8>& bytes,
                 std::initializer_list<unsigned char> signature) {
    return std::equal(signature.begin(), signature.end(), bytes.begin());
}

bool cancelled(const Cancelled& check) {
    return check && check();
}

bool is_encryption_error(const char* message) {
    if (message == nullptr) return false;
    std::string text(message);
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text.find("encrypted") != std::string::npos ||
           text.find("passphrase") != std::string::npos ||
           text.find("password") != std::string::npos;
}

Result fail(Failure failure, ContainerFormat format, std::string message) {
    return Result{failure, format, std::move(message)};
}

std::string normalize_path(const char* raw) {
    if (raw == nullptr || *raw == '\0') return {};
    std::string path(raw);
    std::replace(path.begin(), path.end(), '\\', '/');
    if (path.empty() || path.front() == '/' || path.find(':') != std::string::npos ||
        path.find('\0') != std::string::npos) return {};
    if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':') return {};

    std::string output;
    std::size_t start = 0;
    while (start <= path.size()) {
        const auto end = path.find('/', start);
        const auto part = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part == "..") return {};
        if (!part.empty() && part != ".") {
            if (!output.empty()) output.push_back('/');
            output.append(part);
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (output.empty()) return {};
    if (path.back() == '/') output.push_back('/');
    return output;
}

std::string identity(const std::string& path) {
    std::string key = path;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    while (!key.empty() && key.back() == '/') key.pop_back();
    return key;
}

bool contained_by(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    auto root_it = root.begin();
    auto candidate_it = candidate.begin();
    for (; root_it != root.end(); ++root_it, ++candidate_it) {
        if (candidate_it == candidate.end() || *root_it != *candidate_it) return false;
    }
    return true;
}

bool ensure_safe_parent(const std::filesystem::path& root,
                        const std::filesystem::path& parent) {
    std::filesystem::path current = root;
    const auto relative = parent.lexically_relative(root);
    for (const auto& component : relative) {
        if (component == "." || component.empty()) continue;
        if (component == "..") return false;
        current /= component;
        std::error_code error;
        const auto status = std::filesystem::symlink_status(current, error);
        if (!error && std::filesystem::is_symlink(status)) return false;
        if (error == std::errc::no_such_file_or_directory || !std::filesystem::exists(status)) {
            error.clear();
            if (!std::filesystem::create_directory(current, error) && error) return false;
        } else if (error || !std::filesystem::is_directory(status)) {
            return false;
        }
        const auto resolved = std::filesystem::weakly_canonical(current, error);
        if (error || !contained_by(root, resolved)) return false;
    }
    return true;
}

bool enough_space(const std::filesystem::path& root,
                  std::uint64_t declared,
                  std::uint64_t actual,
                  std::uint64_t reserve) {
    std::error_code error;
    const auto info = std::filesystem::space(root, error);
    if (error) return false;
    const auto remaining = declared > actual ? declared - actual : 0;
    if (remaining > std::numeric_limits<std::uint64_t>::max() - reserve) return false;
    return info.available >= remaining + reserve;
}

} // namespace

ContainerFormat detect_format(const std::filesystem::path& input) {
    std::ifstream file(input, std::ios::binary);
    if (!file) return ContainerFormat::unsupported;
    std::array<unsigned char, 8> bytes{};
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    const auto count = file.gcount();
    if (count >= 6 && starts_with(bytes, {0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c})) return ContainerFormat::seven_zip;
    if (count >= 8 && starts_with(bytes, {0x52, 0x61, 0x72, 0x21, 0x1a, 0x07, 0x01, 0x00})) return ContainerFormat::rar5;
    if (count >= 7 && starts_with(bytes, {0x52, 0x61, 0x72, 0x21, 0x1a, 0x07, 0x00})) return ContainerFormat::rar4;
    if (count >= 4 && (starts_with(bytes, {0x50, 0x4b, 0x03, 0x04}) ||
                       starts_with(bytes, {0x50, 0x4b, 0x05, 0x06}) ||
                       starts_with(bytes, {0x50, 0x4b, 0x07, 0x08}))) return ContainerFormat::zip;
    return ContainerFormat::unsupported;
}

Result extract_archive(const std::filesystem::path& input,
                       const std::filesystem::path& private_staging_root,
                       const Limits& limits,
                       const Cancelled& is_cancelled,
                       const ProgressCallback& progress) {
    const auto format = detect_format(input);
    if (format == ContainerFormat::unsupported)
        return fail(Failure::unsupported_format, format, "Archive signature is not supported");
    if (limits.max_decoder_memory_bytes < kDecoderPeakCapBytes)
        return fail(Failure::decoder_memory_limit, format, "Configured memory budget is below the decoder's bounded peak");
    if (cancelled(is_cancelled)) return fail(Failure::cancelled, format, "Archive expansion was cancelled");

    std::error_code fs_error;
    const auto supplied_root_status = std::filesystem::symlink_status(private_staging_root, fs_error);
    if (!fs_error && std::filesystem::exists(supplied_root_status))
        return fail(Failure::io_error, format, "Private staging directory must be new and empty");
    if (fs_error != std::errc::no_such_file_or_directory && fs_error)
        return fail(Failure::io_error, format, "Could not inspect private staging directory");
    if (!fs_error && std::filesystem::is_symlink(supplied_root_status))
        return fail(Failure::unsafe_path, format, "Private staging root cannot be a symbolic link");
    fs_error.clear();
    std::filesystem::create_directory(private_staging_root, fs_error);
    if (fs_error) return fail(Failure::io_error, format, "Could not create private staging directory");
    const auto root = std::filesystem::canonical(private_staging_root, fs_error);
    if (fs_error || std::filesystem::is_symlink(std::filesystem::symlink_status(root, fs_error)))
        return fail(Failure::unsafe_path, format, "Private staging directory is not a real directory");

    struct RollbackOnFailure {
        const std::filesystem::path& root;
        bool keep = false;
        ~RollbackOnFailure() {
            if (!keep) {
                std::error_code ignored;
                std::filesystem::remove_all(root, ignored);
            }
        }
    } rollback{root};

    archive* raw_reader = archive_read_new();
    if (raw_reader == nullptr) return fail(Failure::io_error, format, "Could not allocate archive reader");
    const auto close_reader = [](archive* value) {
        if (value != nullptr) {
            archive_read_close(value);
            archive_read_free(value);
        }
    };
    std::unique_ptr<archive, decltype(close_reader)> reader(raw_reader, close_reader);
    archive_read_support_filter_none(reader.get());
    archive_read_support_format_zip(reader.get());
    archive_read_support_format_rar(reader.get());
    archive_read_support_format_rar5(reader.get());
    archive_read_support_format_7zip(reader.get());
    if (archive_read_open_filename(reader.get(), input.string().c_str(), 64 * 1024) != ARCHIVE_OK)
        return fail(Failure::malformed_archive, format, archive_error_string(reader.get()) != nullptr
            ? archive_error_string(reader.get()) : "Could not open archive");

    std::set<std::string> identities;
    std::uint64_t declared_total = 0;
    std::uint64_t actual_total = 0;
    std::uint32_t entry_count = 0;
    std::array<char, kCopyBufferBytes> buffer{};
    archive_entry* entry = nullptr;
    for (;;) {
        if (cancelled(is_cancelled)) return fail(Failure::cancelled, format, "Archive expansion was cancelled");
        const int header_status = archive_read_next_header(reader.get(), &entry);
        if (header_status == ARCHIVE_EOF) break;
        if (header_status != ARCHIVE_OK) {
            const char* message = archive_error_string(reader.get());
            return fail(is_encryption_error(message) ? Failure::encrypted_archive : Failure::malformed_archive,
                        format,
                        message != nullptr ? message : "Archive header is malformed");
        }
        if (archive_entry_is_encrypted(entry) > 0)
            return fail(Failure::encrypted_archive, format, "Encrypted archive entries are unsupported");

        if (++entry_count > limits.max_entries)
            return fail(Failure::entry_count_limit, format, "Archive entry count limit exceeded");
        const char* pathname = archive_entry_pathname_utf8(entry);
        if (pathname == nullptr) pathname = archive_entry_pathname(entry);
        const auto safe_path = normalize_path(pathname);
        if (safe_path.empty()) return fail(Failure::unsafe_path, format, "Archive member path is unsafe");
        if (!identities.insert(identity(safe_path)).second)
            return fail(Failure::duplicate_entry, format, "Archive has duplicate normalized member paths");
        if (archive_entry_symlink(entry) != nullptr || archive_entry_hardlink(entry) != nullptr)
            return fail(Failure::link_or_special_entry, format, "Archive links are not accepted");

        const auto type = archive_entry_filetype(entry);
        if (type != AE_IFREG && type != AE_IFDIR)
            return fail(Failure::link_or_special_entry, format, "Archive contains a special file");
        const auto entry_size = archive_entry_size_is_set(entry) ? archive_entry_size(entry) : -1;
        if (entry_size < 0) return fail(Failure::expanded_bytes_limit, format, "Archive member size is unknown");
        if (static_cast<std::uint64_t>(entry_size) > limits.max_expanded_bytes - declared_total)
            return fail(Failure::expanded_bytes_limit, format, "Archive declared expansion exceeds limit");
        declared_total += static_cast<std::uint64_t>(entry_size);
        if (!enough_space(root, declared_total, actual_total, limits.free_space_reserve_bytes))
            return fail(Failure::insufficient_space, format, "Not enough free space for archive staging");

        const auto target = root / std::filesystem::path(safe_path);
        const auto parent = std::filesystem::weakly_canonical(target.parent_path(), fs_error);
        if (fs_error || !contained_by(root, parent) || !ensure_safe_parent(root, parent))
            return fail(Failure::unsafe_path, format, "Archive member parent escaped private staging");
        if (type == AE_IFDIR) continue;
        if (std::filesystem::exists(target, fs_error) || std::filesystem::is_symlink(std::filesystem::symlink_status(target, fs_error)))
            return fail(Failure::duplicate_entry, format, "Archive output path already exists");

        auto partial = target;
        partial += "." + std::to_string(entry_count) + ".partial";
        if (std::filesystem::exists(partial, fs_error) || fs_error) {
            return fail(Failure::duplicate_entry, format, "Temporary archive output already exists");
        }
        std::ofstream output(partial, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output) return fail(Failure::io_error, format, "Could not create temporary archive output");
        std::uint64_t entry_actual = 0;
        for (;;) {
            if (cancelled(is_cancelled)) {
                output.close();
                std::filesystem::remove(partial, fs_error);
                return fail(Failure::cancelled, format, "Archive expansion was cancelled");
            }
            const auto count = archive_read_data(reader.get(), buffer.data(), buffer.size());
            if (count == 0) break;
            if (count < 0) {
                output.close();
                std::filesystem::remove(partial, fs_error);
                return fail(Failure::malformed_archive, format, archive_error_string(reader.get()) != nullptr
                    ? archive_error_string(reader.get()) : "Archive member decode failed");
            }
            const auto decoded = static_cast<std::uint64_t>(count);
            if (decoded > limits.max_expanded_bytes - actual_total) {
                output.close();
                std::filesystem::remove(partial, fs_error);
                return fail(Failure::expanded_bytes_limit, format, "Archive decoded bytes exceed limit");
            }
            actual_total += decoded;
            entry_actual += decoded;
            output.write(buffer.data(), count);
            if (!output || !enough_space(root, declared_total, actual_total, limits.free_space_reserve_bytes)) {
                output.close();
                std::filesystem::remove(partial, fs_error);
                return fail(output ? Failure::insufficient_space : Failure::io_error, format, "Could not stage decoded archive bytes");
            }
            if (progress) progress(Progress{safe_path, entry_actual, actual_total});
        }
        output.flush();
        output.close();
        if (!output || entry_actual != static_cast<std::uint64_t>(entry_size)) {
            std::filesystem::remove(partial, fs_error);
            return fail(Failure::malformed_archive, format, "Decoded member size does not match its header");
        }
        if (std::filesystem::exists(target, fs_error)) {
            std::filesystem::remove(partial, fs_error);
            return fail(Failure::duplicate_entry, format, "Archive output path already exists");
        }
        std::filesystem::rename(partial, target, fs_error);
        if (fs_error) {
            std::filesystem::remove(partial, fs_error);
            return fail(Failure::io_error, format, "Could not atomically promote archive member");
        }
    }
    rollback.keep = true;
    return Result{Failure::none, format, "Archive members staged"};
}

} // namespace emucorev::archive_expansion
