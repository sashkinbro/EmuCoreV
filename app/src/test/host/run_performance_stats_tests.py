#!/usr/bin/env python3
"""Exercise the production telemetry header concurrently, without an Android session."""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
out = root / "app/build/performance-stats-probe"
out.mkdir(parents=True, exist_ok=True)
source = out / "probe.cpp"
source.write_text(r'''
#include <renderer/performance_stats.h>
#include <thread>
#include <cstdio>

int main() {
    renderer::PerformanceStatsTelemetry telemetry;
    if (telemetry.frame_metrics.load() || telemetry.updated_ms.load()) return 1;
    std::atomic<bool> done{false};
    std::atomic<unsigned> bad{0};
    std::thread publisher([&] {
        for (uint32_t n = 1; n <= 100000; ++n) telemetry.publish(n, n * 2);
        done.store(true, std::memory_order_release);
    });
    do {
        auto pair = telemetry.frame_metrics.load(std::memory_order_acquire);
        if (uint32_t(pair >> 32) != uint32_t(pair) * 2) ++bad;
    } while (!done.load(std::memory_order_acquire));
    publisher.join();
    if (bad || telemetry.updated_ms.load() <= 0 || uint32_t(telemetry.frame_metrics.load()) != 100000) return 2;
    telemetry.reset();
    if (telemetry.frame_metrics.load() || telemetry.updated_ms.load()) return 3;
    telemetry.publish(60, 17);
    auto resumed = telemetry.frame_metrics.load(std::memory_order_acquire);
    if (uint32_t(resumed) != 60 || uint32_t(resumed >> 32) != 17 || telemetry.updated_ms.load() <= 0) return 4;
    std::puts("PASS: coherent concurrent FPS pair; cleared metrics on reset; fresh metrics after resume");
}
''', encoding="utf-8")
compiler = shutil.which("g++") or "C:/msys64/ucrt64/bin/g++.exe"
env = os.environ.copy()
env["PATH"] = str(Path(compiler).parent) + os.pathsep + env["PATH"]
exe = out / "probe.exe"
subprocess.run([compiler, "-std=c++20", "-O2", "-pthread", "-I" + str(root / "app/src/main/cpp/vita3k/vita3k/renderer/include"),
                str(source), "-o", str(exe)], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
