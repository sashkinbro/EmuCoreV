// fw_ref <pup> <vita_root>: runs the vendored core install_pup (the pre-B030 behaviour) and prints version, peak RSS and
// the sampled peak size of the Vita root (disk high-water mark incl. PUP_DEC).
#include "measure.h"

#include <packages/functions.h>
#include <util/fs.h>

#include <cstdio>
#include <exception>

int main(int argc, char **argv) {
    if (argc < 3)
        return 2;
    const auto root = fs_utils::utf8_to_path(argv[2]);
    fs::create_directories(root);
    DiskPeakSampler sampler(root);
    int rc = 0;
    try {
        const auto v = install_pup(root, fs_utils::utf8_to_path(argv[1]));
        std::printf("version=%s\n", v.c_str());
    } catch (const std::exception &e) {
        std::printf("exception=%s\n", e.what());
        rc = 1;
    }
    std::printf("peak_rss_bytes=%llu\npeak_disk_bytes=%llu\n", peak_rss_bytes(), static_cast<unsigned long long>(sampler.stop()));
    return rc;
}
