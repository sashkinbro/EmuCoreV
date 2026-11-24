// EmuCoreV adapter layer: robust PS Vita firmware (PUP) installer.
//
// Replaces the vendored `install_pup` for the Android app (B030-B037): the PUP is validated before anything is written,
// decrypted straight from the package into one partition image at a time (bounded memory, no intermediate copies),
// extracted into a staging directory and only then swapped into the Vita root with renames. A failed, cancelled or
// crashed install leaves the previous firmware untouched. A completion marker (`fw.complete`) is written last.
//
// No JNI in here: the host test harness (app/src/test/fwinstall-host) links this file directly.
#pragma once

#include <util/fs.h>

#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace emucorev::firmware {

enum class Status {
    Ok,
    NotPup, // not a SCEUF package at all
    Truncated, // headers promise more bytes than the file holds
    Corrupt, // structurally invalid (bad record, bad SCE header, failed inflate, missing key file ...)
    IoError, // reading/writing/renaming failed
    NoSpace, // not enough free storage (preflight or ENOSPC while writing)
    Cancelled,
    Internal, // unexpected exception
};

// Stable names, also the first token of the string handed to Kotlin ("<NAME>|<payload>").
const char *status_name(Status status);

enum class Phase { Validate, Decrypt, Extract, Commit };

struct Progress {
    float percent = 0.f; // 0..100, monotonic
    Phase phase = Phase::Validate;
    std::string partition; // "os0", "vs0", "sa0", "pd0" (empty for Validate/Commit)
    uint32_t partition_index = 0; // 1-based position among the partitions of this package
    uint32_t partition_count = 0;
    uint64_t files = 0; // files extracted so far (all partitions)
};

struct Options {
    // Called from the installing thread; may throw (the install is then rolled back and reported as Internal).
    std::function<void(const Progress &)> progress;
    // Polled between I/O chunks. Returning true unwinds cleanly (staging removed, old tree kept) -> Status::Cancelled.
    std::function<bool()> cancelled;
    // Test seam: free bytes on the volume of the Vita root. Default: fs::space(root).available.
    std::function<uint64_t()> available_bytes;
};

struct Result {
    Status status = Status::Internal;
    std::string version; // version.txt of the package (Ok only; may be empty when the package has none)
    std::string detail; // human readable English diagnostic, for logs
    uint64_t required_bytes = 0; // set for Status::NoSpace
    std::map<std::string, uint64_t> files; // per installed partition (Ok only)
};

// Installs `pup_path` into `vita_root` (os0/pd0/vs0/sa0 are replaced as whole partitions, as on the console).
// Never throws.
Result install(const fs::path &vita_root, const fs::path &pup_path, const Options &options = {});

// Rolls back a commit that was interrupted by a crash (restores partitions kept in `.fw_old`) and removes leftovers
// (`.fw_stage`, legacy `PUP_DEC`). Called by install(); exposed for tools/tests.
void recover_interrupted(const fs::path &vita_root);

constexpr const char *kMarkerName = "fw.complete";

} // namespace emucorev::firmware
