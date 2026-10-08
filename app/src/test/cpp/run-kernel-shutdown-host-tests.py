"""Exercise the production process_exit body at its registry concurrency boundary."""
from pathlib import Path
import os
import subprocess
import tempfile

here = Path(__file__).resolve().parent
vendor = here.parents[1] / "main/cpp/vita3k"
source = (vendor / "vita3k/kernel/src/kernel.cpp").read_text()
signature = "void KernelState::process_exit()"
start = source.index(signature)
opening = source.index("{", start)
depth = 1
end = opening + 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
body = source[start:end]
fixture = r'''
#include <gtest/gtest.h>
#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <kernel/host_threads.h>
#include <mem/block.h>
struct MemState {};
struct KernelState;
struct ThreadState {
    int id;
    std::mutex mutex;
    bool deleted = false;
    Block stack;
    ThreadState(int id, KernelState &, MemState &) : id(id) {}
    void exit_delete(bool) { std::lock_guard lock(mutex); deleted = true; }
    bool is_delete_requested() const { return deleted; }
};
using ThreadStatePtr = std::shared_ptr<ThreadState>;
struct KernelState {
    std::mutex mutex;
    std::condition_variable thread_deleted_cond;
    std::map<int, ThreadStatePtr> threads;
    HostThreadRegistry host_threads;
    std::atomic<uint64_t> sync_cache_generation{ 0 };
    void process_exit();
};
void clear_sync_primitive_thread_cache() {}
struct KernelShutdownEnv { KernelState kernel; MemState mem; };
using KernelHostLifetimeEnv = KernelShutdownEnv;
'''
gtest = vendor / "external/googletest/googletest"
cxx = "C:/msys64/ucrt64/bin/g++.exe"
env = dict(os.environ)
env["PATH"] = str(Path(cxx).parent) + os.pathsep + env.get("PATH", "")
with tempfile.TemporaryDirectory(prefix="emucorev-shutdown-") as temporary:
    generated = Path(temporary) / "shutdown.cpp"
    generated.write_text(fixture + body + '\n#include "' + (here.parent / "core-api/kernel_shutdown_tests.inc").as_posix() + '"\n#include "' + (here.parent / "core-api/kernel_host_lifetime_tests.inc").as_posix() + '"\n')
    exe = Path(temporary) / "shutdown.exe"
    includes = [vendor / "vita3k" / name / "include" for name in ("kernel", "mem", "util")]
    subprocess.run([cxx, "-std=c++23", "-pthread", *["-I" + str(p) for p in includes], "-I" + str(gtest / "include"), "-I" + str(gtest), str(generated), str(gtest / "src/gtest-all.cc"), str(gtest / "src/gtest_main.cc"), "-o", str(exe)], check=True, env=env, timeout=60)
    subprocess.run([str(exe)], check=True, env=env, timeout=10)
