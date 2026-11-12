"""Exercise the production RIF reader and content-ID validation with host fixtures."""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
out = root / "app/build/license-read-probe"
out.mkdir(parents=True, exist_ok=True)
if os.name == "nt" and Path("C:/msys64/ucrt64/bin").is_dir():
    os.environ["PATH"] = "C:/msys64/ucrt64/bin;" + os.environ.get("PATH", "")
source = (root / "app/src/main/cpp/vita3k/vita3k/packages/src/license.cpp").read_text()


def block(marker):
    assert source.count(marker) == 1, marker
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if depth == 0:
            return source[start:end + 1]
    raise AssertionError(marker)


cpp = r'''
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
namespace fs {
using path = std::filesystem::path;
using ifstream = std::ifstream;
using std::filesystem::copy_file;
using std::filesystem::copy_options;
using std::filesystem::create_directories;
using std::filesystem::exists;
using std::filesystem::is_regular_file;
using std::filesystem::remove_all;
using std::filesystem::temp_directory_path;
}
struct SceNpDrmLicense { char content_id[0x30]; char rest[0x1d0]; };
static_assert(sizeof(SceNpDrmLicense) == 0x200);
struct EmuEnvState {
    fs::path vita_fs_path;
    std::string license_content_id;
    std::string license_title_id;
};
namespace fmt {
template <typename T> std::string format(const std::string &, const T &value) { return value + ".rif"; }
}
#define LOG_ERROR(...) ((void)0)
#define LOG_INFO(...) ((void)0)
'''
for marker in (
    "static bool open_license",
    "static bool get_license_content_ids",
    "bool copy_license",
):
    cpp += block(marker) + "\n"
cpp += r'''
static void write_license(const fs::path &path, const std::string &content_id, bool terminated = true) {
    SceNpDrmLicense lic{};
    std::fill(std::begin(lic.content_id), std::end(lic.content_id), '\0');
    const auto n = std::min(content_id.size(), sizeof(lic.content_id));
    std::memcpy(lic.content_id, content_id.data(), n);
    if (!terminated && n == sizeof(lic.content_id)) {
        // All 48 bytes remain occupied, exercising the bounded terminator check.
    }
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(&lic), sizeof(lic));
}
static bool check(bool ok, const char *name) {
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    return ok;
}
int main() {
    const fs::path root = fs::temp_directory_path() / "emucorev-license-read-probe";
    fs::remove_all(root);
    fs::create_directories(root);
    EmuEnvState env{root / "vita", "previous-content", "PREVIOUS01"};
    const fs::path valid = root / "valid.rif";
    const std::string expected = "UP0006-PCSE00089_00-NFSMW000VITA00NA";
    write_license(valid, expected);
    const bool valid_accepted = copy_license(env, valid);
    bool passed = true;
    passed &= check(valid_accepted && env.license_content_id == expected && env.license_title_id == "PCSE00089",
                    "valid retail RIF copied and IDs extracted");
    const auto installed = env.vita_fs_path / "ux0/license/PCSE00089" / (expected + ".rif");
    passed &= check(fs::is_regular_file(installed), "valid RIF installed at title-scoped path");

    const fs::path indexed_content = root / "indexed-content.rif";
    const std::string indexed_id = "UP0006-PCSE00089_01-nfsmw000vita00na";
    write_license(indexed_content, indexed_id);
    const bool indexed_accepted = copy_license(env, indexed_content);
    passed &= check(indexed_accepted && env.license_content_id == indexed_id &&
                    env.license_title_id == "PCSE00089" &&
                    fs::is_regular_file(env.vita_fs_path / "ux0/license/PCSE00089" / (indexed_id + ".rif")),
                    "nonzero content index and lowercase product suffix accepted");

    auto rejected_without_mutation = [&](const fs::path &path, const char *label) {
        env.license_content_id = "previous-content";
        env.license_title_id = "PREVIOUS01";
        const bool rejected = !copy_license(env, path);
        const bool unchanged = env.license_content_id == "previous-content" && env.license_title_id == "PREVIOUS01";
        return check(rejected && unchanged, label);
    };

    const fs::path short_file = root / "truncated.rif";
    { std::ofstream f(short_file, std::ios::binary); f << "short"; }
    passed &= rejected_without_mutation(short_file, "truncated RIF rejected without changing selected IDs");

    const fs::path unterminated = root / "unterminated.rif";
    write_license(unterminated, std::string(48, 'A'), false);
    passed &= rejected_without_mutation(unterminated, "unterminated content ID rejected within fixed buffer bounds");

    const fs::path short_id = root / "short-id.rif";
    write_license(short_id, "X");
    passed &= rejected_without_mutation(short_id, "short content ID rejected without throwing");

    const fs::path malformed_id = root / "malformed-id.rif";
    write_license(malformed_id, "UP0006-../00000_00-NFSMW000VITA00NA");
    passed &= rejected_without_mutation(malformed_id, "unsafe content ID rejected before path creation");
    passed &= check(!fs::exists(env.vita_fs_path / "ux0/license/../00000_00-NFSMW000VITA00NA"),
                    "malformed content ID does not create a license destination");

    fs::remove_all(root);
    return passed ? 0 : 1;
}
'''
(out / "probe.cpp").write_text(cpp)
compiler = os.environ.get("CXX") or shutil.which("g++")
if not compiler and os.name == "nt" and Path("C:/msys64/ucrt64/bin/g++.exe").is_file():
    compiler = "C:/msys64/ucrt64/bin/g++.exe"
assert compiler, "A C++ compiler is required"
subprocess.run([compiler, "-std=c++20", "-O0", str(out / "probe.cpp"), "-o", str(out / "probe.exe")], check=True)
result = subprocess.run([str(out / "probe.exe")], capture_output=True, text=True)
print(result.stdout + result.stderr)
(out / ("red.log" if result.returncode else "green.log")).write_text(result.stdout + result.stderr)
raise SystemExit(result.returncode)
