#!/usr/bin/env python3
"""Exercise production fd read/close code under forced overlap."""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
source = (root / 'app/src/main/cpp/vita3k/vita3k/io/src/io.cpp').read_text(encoding='utf-8')


def function_body(signature: str) -> str:
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    state = 'code'
    escaped = False
    i = opening
    while i < len(source):
        char = source[i]
        following = source[i + 1] if i + 1 < len(source) else ''
        if state == 'line_comment':
            if char == '\n':
                state = 'code'
        elif state == 'block_comment':
            if char == '*' and following == '/':
                state = 'code'
                i += 1
        elif state == 'string':
            if escaped:
                escaped = False
            elif char == '\\':
                escaped = True
            elif char == '"':
                state = 'code'
        elif state == 'char':
            if escaped:
                escaped = False
            elif char == '\\':
                escaped = True
            elif char == "'":
                state = 'code'
        elif char == '/' and following == '/':
            state = 'line_comment'
            i += 1
        elif char == '/' and following == '*':
            state = 'block_comment'
            i += 1
        elif char == '"':
            state = 'string'
        elif char == "'":
            state = 'char'
        elif char == '{':
            depth += 1
        elif char == '}':
            depth -= 1
            if depth == 0:
                return source[start:i + 1]
        i += 1
    raise AssertionError(f'unterminated function: {signature}')


read_file = function_body('int read_file(void *data, IOState &io,')
close_file = function_body('int close_file(IOState &io,')
insert_fd = function_body('template <typename Map, typename Value>\nstatic SceUID insert_io_fd(IOState &io, Map &entries, Value &&value)')
out = root / 'app/build/io-fd-lifetime-probe'
out.mkdir(parents=True, exist_ok=True)
cpp = r'''
#include <atomic>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

using SceUID = int;
using SceSize = unsigned int;
constexpr int TTY_IN = 1;
constexpr int SCE_ERROR_ERRNO_EBADFD = -9;
constexpr int SCE_ERROR_ERRNO_EMFILE = -24;
#define LOG_TRACE_IF(...) ((void)0)
#define LOG_DEBUG_IF(...) ((void)0)
#define LOG_WARN(...) ((void)0)
#define IO_ERROR(code) (code)
#define IO_ERROR_UNK() (-1)
static int log_hex(int value) { return value; }

struct ReadProbe {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false;
    bool release = false;
    std::atomic<bool> destroyed{false};
    std::atomic<bool> destroyed_during_read{false};
};

struct FileStats {
    std::shared_ptr<ReadProbe> probe;
    explicit FileStats(std::shared_ptr<ReadProbe> p) : probe(std::move(p)) {}
    ~FileStats() { probe->destroyed = true; }
    int read(void *, int, SceSize size) {
        auto held_probe = probe;
        {
            std::lock_guard lock(held_probe->mutex);
            held_probe->entered = true;
        }
        held_probe->changed.notify_all();
        std::unique_lock lock(held_probe->mutex);
        held_probe->changed.wait(lock, [&] { return held_probe->release; });
        if (held_probe->destroyed.load())
            held_probe->destroyed_during_read = true;
        return static_cast<int>(size);
    }
};

struct IOState {
    std::mutex file_mutex;
    int next_fd = 0;
    std::map<SceUID, FileStats> std_files;
    std::map<SceUID, int> tty_files;
};
''' + insert_fd + '\n' + read_file + '\n' + close_file + r'''

int main() {
    std::mutex ids_mutex;
    std::vector<int> ids;
    IOState allocator_state;
    std::map<int, int> generated_fds;
    std::vector<std::thread> allocators;
    for (int thread = 0; thread < 8; ++thread) {
        allocators.emplace_back([&, thread] {
            std::vector<int> local;
            for (int i = 0; i < 1000; ++i)
                local.push_back(insert_io_fd(allocator_state, generated_fds, thread * 1000 + i));
            std::lock_guard lock(ids_mutex);
            ids.insert(ids.end(), local.begin(), local.end());
        });
    }
    for (auto &allocator : allocators)
        allocator.join();
    std::sort(ids.begin(), ids.end());
    const bool unique_ids = ids.size() == 8000
        && generated_fds.size() == 8000
        && std::adjacent_find(ids.begin(), ids.end()) == ids.end()
        && ids.front() == 0 && ids.back() == 7999;

    auto probe = std::make_shared<ReadProbe>();
    IOState io;
    io.std_files.emplace(std::piecewise_construct,
        std::forward_as_tuple(42), std::forward_as_tuple(probe));
    char byte = 0;
    int read_result = -1;
    std::atomic<bool> close_started{false};
    std::atomic<bool> close_finished{false};
    std::thread reader([&] { read_result = read_file(&byte, io, 42, 1, "fd-test"); });
    {
        std::unique_lock lock(probe->mutex);
        probe->changed.wait(lock, [&] { return probe->entered; });
    }
    std::thread closer([&] {
        close_started = true;
        close_file(io, 42, "fd-test");
        close_finished = true;
    });
    while (!close_started.load())
        std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    const bool close_waited_for_read = !close_finished.load();
    {
        std::lock_guard lock(probe->mutex);
        probe->release = true;
    }
    probe->changed.notify_all();
    reader.join();
    closer.join();

    const bool read_succeeded = read_result == 1;
    const bool object_survived_read = !probe->destroyed_during_read.load();
    std::printf("%s close waits until the in-flight read releases its fd entry\n",
        close_waited_for_read ? "PASS" : "FAIL");
    std::printf("%s fd object remains alive for the full read call\n",
        object_survived_read ? "PASS" : "FAIL");
    std::printf("%s read returns the requested byte count\n",
        read_succeeded ? "PASS" : "FAIL");
    std::printf("%s concurrent open paths receive distinct fd values\n",
        unique_ids ? "PASS" : "FAIL");
    return close_waited_for_read && object_survived_read && read_succeeded && unique_ids ? 0 : 1;
}
'''
(out / 'probe.cpp').write_text(cpp, encoding='utf-8')
if os.name == 'nt':
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler, 'host C++ compiler required'
subprocess.run([compiler, '-std=c++20', '-O0', '-pthread', str(out / 'probe.cpp'), '-o', str(out / 'probe.exe')], check=True)
result = subprocess.run([str(out / 'probe.exe')], capture_output=True, text=True)
print(result.stdout + result.stderr, end='')
(out / ('red.log' if result.returncode else 'green.log')).write_text(result.stdout + result.stderr)
raise SystemExit(result.returncode)
