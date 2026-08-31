// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include <util/mem_snapshot.h>

#include <util/log.h>

std::atomic<int> mem_diag::pending_trim_level{ -1 };

#if defined(__linux__)

#include <cctype>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace mem_diag {

static bool read_small_file(const char *path, char *buf, size_t cap) {
    FILE *f = fopen(path, "re");
    if (!f)
        return false;
    const size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = '\0';
    return true;
}

static uint64_t field_value(const char *buf, const char *name) {
    const size_t name_len = strlen(name);
    const char *p = buf;
    while ((p = strstr(p, name)) != nullptr) {
        if ((p == buf || p[-1] == '\n') && p[name_len] == ':')
            return strtoull(p + name_len + 1, nullptr, 10);
        p += name_len;
    }
    return UINT64_MAX;
}

static void dmabuf_totals(uint32_t &count, uint64_t &bytes) {
    count = 0;
    bytes = 0;
    DIR *dir = opendir("/proc/self/fd");
    if (!dir)
        return;
    const int dir_fd = dirfd(dir);
    dirent *ent;
    char link_buf[128], path_buf[64], info_buf[1024];
    while ((ent = readdir(dir)) != nullptr) {
        if (!isdigit(static_cast<unsigned char>(ent->d_name[0])))
            continue;
        const ssize_t n = readlinkat(dir_fd, ent->d_name, link_buf, sizeof(link_buf) - 1);
        if (n <= 0)
            continue;
        link_buf[n] = '\0';
        if (strstr(link_buf, "dmabuf") == nullptr)
            continue;
        count++;
        uint64_t size = UINT64_MAX;
        snprintf(path_buf, sizeof(path_buf), "/proc/self/fdinfo/%s", ent->d_name);
        if (read_small_file(path_buf, info_buf, sizeof(info_buf)))
            size = field_value(info_buf, "size"); // bytes in fdinfo
        if (size == UINT64_MAX || size == 0) {
            struct stat st{};
            if (fstatat(dir_fd, ent->d_name, &st, 0) == 0 && st.st_size > 0)
                size = static_cast<uint64_t>(st.st_size);
        }
        if (size != UINT64_MAX)
            bytes += size;
    }
    closedir(dir);
}

void log_memory_snapshot(const char *tag) {
    const auto mib = [](uint64_t kb) -> int64_t {
        return (kb == UINT64_MAX) ? -1 : static_cast<int64_t>(kb / 1024);
    };

    char buf[4096];
    uint64_t sys_avail = UINT64_MAX, sys_free = UINT64_MAX, swap_total = UINT64_MAX, swap_free = UINT64_MAX;
    if (read_small_file("/proc/meminfo", buf, sizeof(buf))) {
        sys_avail = field_value(buf, "MemAvailable");
        sys_free = field_value(buf, "MemFree");
        swap_total = field_value(buf, "SwapTotal");
        swap_free = field_value(buf, "SwapFree");
    }

    uint64_t rss = UINT64_MAX, rss_anon = UINT64_MAX, rss_file = UINT64_MAX, rss_shmem = UINT64_MAX, vm_swap = UINT64_MAX;
    if (read_small_file("/proc/self/status", buf, sizeof(buf))) {
        rss = field_value(buf, "VmRSS");
        rss_anon = field_value(buf, "RssAnon");
        rss_file = field_value(buf, "RssFile");
        rss_shmem = field_value(buf, "RssShmem");
        vm_swap = field_value(buf, "VmSwap");
    }

    long oom_adj = LONG_MIN;
    if (read_small_file("/proc/self/oom_score_adj", buf, sizeof(buf)))
        oom_adj = strtol(buf, nullptr, 10);
    else if (read_small_file("/proc/self/oom_score", buf, sizeof(buf)))
        oom_adj = strtol(buf, nullptr, 10);

    uint32_t dmabuf_count = 0;
    uint64_t dmabuf_bytes = 0;
    dmabuf_totals(dmabuf_count, dmabuf_bytes);

    LOG_INFO("[MEMSNAP] {}: sys avail={} free={} swapFree={}/{} MiB | proc RSS={} MiB (anon={} file={} shmem={}) vmSwap={} | dmabuf {} fds {} MiB | oom_adj={}",
        tag, mib(sys_avail), mib(sys_free), mib(swap_free), mib(swap_total),
        mib(rss), mib(rss_anon), mib(rss_file), mib(rss_shmem), mib(vm_swap),
        dmabuf_count, dmabuf_bytes / (1024 * 1024),
        (oom_adj == LONG_MIN) ? std::string("?") : std::to_string(oom_adj));
}

} // namespace mem_diag

#else // !__linux__

namespace mem_diag {

void log_memory_snapshot(const char *) {
}

} // namespace mem_diag

#endif
