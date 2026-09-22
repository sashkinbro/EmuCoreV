#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <util/fs.h>

struct EmuEnvState;

namespace emucorev::savestate {

constexpr uint32_t kEngineVersion = 10;

struct Meta {
    uint32_t format_version = 0;
    uint32_t engine_version = 0;
    uint32_t session_token = 0;
    uint64_t timestamp_ms = 0;
    uint32_t core_flags = 0;
    std::string title_id;
    std::string app_version;
    std::string engine_revision;
};

enum class Status {
    Ok,
    NotRunning,
    InvalidFile,
    UnsupportedVersion,
    TitleMismatch,
    SessionMismatch,
    NoSpace,
    IoError,
    InternalError,
};

struct Result {
    Status status = Status::Ok;
    std::string error;
    uint64_t bytes = 0;
    Meta meta;
    bool session_match = true;

    bool ok() const { return status == Status::Ok; }
};

using ProgressCallback = std::function<void(float progress, const char *stage)>;

Result save_state(EmuEnvState &emuenv, const fs::path &path, const std::string &app_version, const ProgressCallback &progress = {});
Result load_state(EmuEnvState &emuenv, const fs::path &path, bool allow_cross_session, const ProgressCallback &progress = {});
Result inspect_state(const fs::path &path);
Result delete_state(const fs::path &path);
bool is_current_session(EmuEnvState &emuenv, const Meta &meta);

const char *status_name(Status status);

} // namespace emucorev::savestate
