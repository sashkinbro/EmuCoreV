// Host measurement helpers: process peak RSS and peak on-disk size of a directory tree (sampled).
#pragma once

#include <util/fs.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
inline unsigned long long peak_rss_bytes() {
    PROCESS_MEMORY_COUNTERS c{};
    GetProcessMemoryInfo(GetCurrentProcess(), &c, sizeof(c));
    return c.PeakWorkingSetSize;
}
#else
#include <sys/resource.h>
inline unsigned long long peak_rss_bytes() {
    rusage u{};
    getrusage(RUSAGE_SELF, &u);
    return static_cast<unsigned long long>(u.ru_maxrss) * 1024;
}
#endif

inline uint64_t directory_bytes(const fs::path &dir) {
    uint64_t total = 0;
    boost::system::error_code ec;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        boost::system::error_code e2;
        if (fs::is_regular_file(it->path(), e2)) {
            const auto size = fs::file_size(it->path(), e2);
            if (!e2)
                total += size;
        }
    }
    return total;
}

// Samples directory_bytes(dir) every 10 ms on a helper thread and remembers the maximum.
class DiskPeakSampler {
public:
    explicit DiskPeakSampler(fs::path dir)
        : dir_(std::move(dir))
        , thread_([this] {
            while (!stop_) {
                const uint64_t now = directory_bytes(dir_);
                uint64_t prev = peak_;
                while (now > prev && !peak_.compare_exchange_weak(prev, now)) {
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }) {}
    ~DiskPeakSampler() { stop(); }
    uint64_t stop() {
        stop_ = true;
        if (thread_.joinable())
            thread_.join();
        return peak_;
    }

private:
    fs::path dir_;
    std::atomic<bool> stop_{ false };
    std::atomic<uint64_t> peak_{ 0 };
    std::thread thread_;
};
