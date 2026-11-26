#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <util/fs.h>

struct EmuEnvState;

namespace emucorev::savestate {

// Engine 20 changed the GXM context layout (back-face visibility index, P8), saves the sceGxmSetYuvProfile
// table (P4) and the renderer's back-face visibility query. Saves are not migrated between engine
// versions: an older (or newer) save is rejected up front with a clear reason, and the file is kept.
// Every struct serialized as raw bytes is size-checked in serialized_layout.h, so a layout change
// fails the build until this version is bumped.
constexpr uint32_t kEngineVersion = 20;
constexpr bool supports_engine_version(uint32_t version) {
    return version == kEngineVersion;
}
// The user-facing reason for a save this build cannot read.
std::string unsupported_engine_version_reason(uint32_t version);

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
    ConfigurationMismatch,
};

struct Result {
    Status status = Status::Ok;
    std::string error;
    uint64_t bytes = 0;
    Meta meta;
    bool session_match = true;
    // False only after a failed destructive restore. The frontend must stop the
    // session instead of resuming partially reconstructed guest/host objects.
    bool session_usable = true;

    bool ok() const { return status == Status::Ok; }
};

using ProgressCallback = std::function<void(float progress, const char *stage)>;

Result save_state(EmuEnvState &emuenv, const fs::path &path, const std::string &app_version, const ProgressCallback &progress = {});
Result load_state(EmuEnvState &emuenv, const fs::path &path, bool allow_cross_session, const ProgressCallback &progress = {});
Result inspect_state(const fs::path &path);
// After the title profile is resolved, before renderer/window creation. Changes
// only this launch's effective config; persisted global/title settings stay intact.
Result prepare_launch_gpu_config(EmuEnvState &emuenv, const fs::path &path);
Result delete_state(const fs::path &path);
bool is_current_session(EmuEnvState &emuenv, const Meta &meta);

const char *status_name(Status status);

} // namespace emucorev::savestate
