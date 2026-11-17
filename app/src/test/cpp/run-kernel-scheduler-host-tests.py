"""Exercise the production guest scheduler gate without a device or JIT."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cxx", default=shutil.which("g++") or "C:/msys64/ucrt64/bin/g++.exe")
args = parser.parse_args()

root = Path(__file__).resolve().parents[4]
source = root / "app/src/main/cpp/emucorev/ECV/ECV/kernel/src/thread.cpp"
text = source.read_text(encoding="utf-8")
start = text.index("// Guest execution gate (config: accurate-thread-scheduling).")
end = text.index("thread_local ThreadState *g_tls_guest_thread", start)
scheduler = text[start:end]

test = r"""
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

using SceInt32 = int;
using SceUID = int;
struct CPUState { std::atomic<bool> stop_requested{ false }; };
void stop(CPUState &cpu) { cpu.stop_requested.store(true, std::memory_order_release); }
#define LOG_INFO(...) ((void)0)
#define LOG_WARN(...) ((void)0)

#define NDEBUG
""" + scheduler + r"""
#undef NDEBUG

static bool wait_until(const std::function<bool()> &predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    return predicate();
}

static int running_count() {
    std::lock_guard<std::mutex> lock(g_sched_mutex);
    return g_sched_running;
}

static void lower_waiter_does_not_evict_holder() {
    guest_sched_set_cores(1);
    CPUState holder_cpu, waiter_cpu;
    std::atomic<bool> holder_ready{ false }, waiter_acquired{ false };
    std::atomic<bool> lower_holder_kept_token{ false };

    std::thread holder([&] {
        sched_acquire(40, 0, true, &holder_cpu, "higher-holder", 1);
        holder_ready.store(true, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds(2)); // exceed the normal slice
        if (!wait_until([] { return g_sched_best_waiter.load(std::memory_order_acquire) == 80; }))
            std::abort();
        sched_maybe_yield(0, true, &holder_cpu);
        lower_holder_kept_token.store(tls_holds_token, std::memory_order_release);
        sched_release_internal(&holder_cpu);
    });
    std::thread waiter([&] {
        while (!holder_ready.load(std::memory_order_acquire)) std::this_thread::yield();
        sched_acquire(80, 0, true, &waiter_cpu, "lower-waiter", 2);
        waiter_acquired.store(true, std::memory_order_release);
        if (running_count() != 1) std::abort();
        sched_release_internal(&waiter_cpu);
    });
    holder.join();
    waiter.join();
    assert(lower_holder_kept_token.load(std::memory_order_acquire));
    assert(waiter_acquired.load(std::memory_order_acquire));
    assert(running_count() == 0);
}

static void higher_waiter_preempts_without_overcommit() {
    guest_sched_set_cores(1);
    CPUState holder_cpu, waiter_cpu;
    std::atomic<bool> holder_ready{ false }, allow_holder_yield{ false };
    std::atomic<bool> holder_released{ false }, waiter_acquired{ false };

    std::thread holder([&] {
        sched_acquire(80, 0, true, &holder_cpu, "lower-holder", 3);
        holder_ready.store(true, std::memory_order_release);
        if (!wait_until([&] { return holder_cpu.stop_requested.load(std::memory_order_acquire); }))
            std::abort();
        // Hold the simulated guest quantum across multiple scheduler rechecks.
        // The waiter must stay blocked instead of using a deadline to overbook.
        while (!allow_holder_yield.load(std::memory_order_acquire))
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        sched_maybe_yield(0, true, &holder_cpu);
        holder_released.store(!tls_holds_token, std::memory_order_release);
        if (tls_holds_token) sched_release_internal(&holder_cpu);
    });
    std::thread waiter([&] {
        while (!holder_ready.load(std::memory_order_acquire)) std::this_thread::yield();
        sched_acquire(40, 0, true, &waiter_cpu, "higher-waiter", 4);
        if (running_count() != 1) std::abort(); // deadline fall-through must not overbook
        waiter_acquired.store(true, std::memory_order_release);
        sched_release_internal(&waiter_cpu);
    });
    if (!wait_until([&] { return holder_cpu.stop_requested.load(std::memory_order_acquire); }))
        std::abort();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(!waiter_acquired.load(std::memory_order_acquire));
    assert(running_count() == 1);
    allow_holder_yield.store(true, std::memory_order_release);
    holder.join();
    waiter.join();
    assert(holder_released.load(std::memory_order_acquire));
    assert(waiter_acquired.load(std::memory_order_acquire));
    assert(running_count() == 0);
}

static void equal_priority_waiter_gets_timeslice_fairness() {
    guest_sched_set_cores(1);
    CPUState holder_cpu, waiter_cpu;
    std::atomic<bool> holder_ready{ false }, holder_yielded{ false }, waiter_acquired{ false };

    std::thread holder([&] {
        sched_acquire(60, 0, true, &holder_cpu, "equal-holder", 5);
        holder_ready.store(true, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if (!wait_until([] { return g_sched_best_waiter.load(std::memory_order_acquire) == 60; }))
            std::abort();
        sched_maybe_yield(0, true, &holder_cpu);
        holder_yielded.store(!tls_holds_token, std::memory_order_release);
        if (tls_holds_token) sched_release_internal(&holder_cpu);
    });
    std::thread waiter([&] {
        while (!holder_ready.load(std::memory_order_acquire)) std::this_thread::yield();
        sched_acquire(60, 0, true, &waiter_cpu, "equal-waiter", 6);
        waiter_acquired.store(true, std::memory_order_release);
        sched_release_internal(&waiter_cpu);
    });
    holder.join();
    waiter.join();
    assert(holder_yielded.load(std::memory_order_acquire));
    assert(waiter_acquired.load(std::memory_order_acquire));
    assert(running_count() == 0);
}

int main() {
    lower_waiter_does_not_evict_holder();
    higher_waiter_preempts_without_overcommit();
    equal_priority_waiter_gets_timeslice_fairness();
    std::cout << "Guest scheduler priority and slot tests passed\n";
}
"""

with tempfile.TemporaryDirectory(prefix="emucorev-kernel-scheduler-") as temporary:
    output = Path(temporary) / "scheduler.exe"
    generated = Path(temporary) / "scheduler.cpp"
    generated.write_text(test, encoding="utf-8")
    command = [args.cxx, "-std=c++23", "-pthread", str(generated), "-o", str(output)]
    env = dict(os.environ)
    env["PATH"] = str(Path(args.cxx).resolve().parent) + os.pathsep + env.get("PATH", "")
    subprocess.run(command, env=env, check=True, timeout=90)
    result = subprocess.run([str(output)], env=env, capture_output=True, text=True, timeout=15)
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    print(result.stdout.strip())
