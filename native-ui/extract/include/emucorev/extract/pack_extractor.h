#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// Builds the shell's resource pack from firmware the user installed. The pack is
// derived on the user's device at firmware-install time (and lazily at shell
// launch); nothing derived from firmware ever ships in the application.
namespace emucorev::extract {

// Bump whenever the extractor output (layout, conversion) changes.
inline constexpr int kExtractorRevision = 1;

struct Result {
    bool ok = false;
    std::string error;                 // set when !ok
    std::vector<std::string> warnings;  // optional content that was absent/unsupported
    std::size_t files = 0;             // files written
};

// percent in [0, 100] and a short stage name.
using ProgressFn = std::function<void(int percent, const char* stage)>;

// True when the installed firmware has the sources the shell needs (cheap check).
bool firmware_present(const std::filesystem::path& vita_root);

// Writes the complete pack into out_dir (created if missing; must not contain a
// previous pack). Reads <vita_root>/vs0 and <vita_root>/sa0 only. Never throws.
Result extract_pack(const std::filesystem::path& vita_root, const std::filesystem::path& out_dir,
    const ProgressFn& progress = {});

}  // namespace emucorev::extract
