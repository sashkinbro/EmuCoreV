#pragma once

#include <util/fs.h>
#include <cstdint>
#include <string>

namespace firmware {
inline fs::path package_version_path(const fs::path &root, uint32_t package_type) {
    return root / "system-packages" / (std::to_string(package_type) + ".version");
}
}

struct SceSblUsSpkgInfo {
    uint32_t size;
    uint32_t version;
    uint8_t status[4];
    int32_t reserved;
};
static_assert(sizeof(SceSblUsSpkgInfo) == 0x10);
