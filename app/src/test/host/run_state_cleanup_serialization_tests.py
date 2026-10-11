#!/usr/bin/env python3
"""Compile the actual Android cleanup lambda against lifecycle latch seams."""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
source = (root / 'app/src/main/cpp/vita3k/vita3k/android/jni/main_android.cpp').read_text(encoding='utf-8')
marker = 'const auto cleanup_launch = [&](const app::AppSessionStopReason reason) {'
assert source.count(marker) == 1, 'cleanup extraction marker must remain unique'
begin = source.index(marker)
brace = source.index('{', begin)
depth = 1
end = brace + 1
while depth:
    assert end < len(source), 'unterminated cleanup lambda'
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
assert source[end] == ';'
cleanup = source[begin:end + 1]
assert all(name in cleanup for name in ('session_controller->stop(reason)', 'reset_android_session_audio(*emuenv)', 'SDL_Quit()')), 'production cleanup changed: review seam'
out = root / 'app/build/state-cleanup-serialization-probe'
out.mkdir(parents=True, exist_ok=True)
cpp = r'''
#include <emucorev/savestate/operation_mutex.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <future>
#include <mutex>
#include <vector>
using namespace std::chrono_literals;
namespace app { enum class AppSessionStopReason { UserRequest, LaunchFailure }; }
struct Env {};
struct Session {
    std::promise<void>* entered = nullptr;
    std::shared_future<void> release;
    std::vector<int> steps;
    void stop(app::AppSessionStopReason) {
        steps.push_back(2);
        if (entered) { entered->set_value(); release.wait(); }
    }
};
thread_local Session* current = nullptr;
void detach_overlay_virtual_controller() { current->steps.push_back(1); }
void reset_android_session_audio(Env&) { current->steps.push_back(3); }
namespace ime { void set_sdl_window(void*) { current->steps.push_back(4); } }
void SDL_DestroyWindow(void*) { current->steps.push_back(5); }
void SDL_Quit() { current->steps.push_back(6); }
void run_cleanup(Session& session, bool startup_owned) {
    current = &session;
    Env env; auto* emuenv = &env; auto* session_controller = &session;
    void* window = &env;
    std::unique_lock startup_state_operation(emucorev::savestate::save_state_operation_mutex(), std::defer_lock);
    if (startup_owned) startup_state_operation.lock();
    CLEANUP_BODY
    cleanup_launch(app::AppSessionStopReason::LaunchFailure);
}
int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* name) { if (!ok) { ++failures; std::printf("FAIL %s\n", name); } };
    {
        std::unique_lock active_jni(emucorev::savestate::save_state_operation_mutex());
        Session session;
        std::promise<void> attempting;
        auto cleanup = std::async(std::launch::async, [&] { attempting.set_value(); run_cleanup(session, false); });
        attempting.get_future().wait();
        check(cleanup.wait_for(80ms) == std::future_status::timeout, "active JNI operation excludes destructive cleanup");
        active_jni.unlock();
        cleanup.get();
        check(session.steps == std::vector<int>({1,2,3,4,5,6}), "cleanup keeps ownership through original full lifecycle order");
    }
    {
        Session session;
        std::promise<void> entered, release;
        session.entered = &entered; session.release = release.get_future().share();
        auto cleanup = std::async(std::launch::async, [&] { run_cleanup(session, false); });
        entered.get_future().wait();
        std::promise<void> attempting;
        auto jni = std::async(std::launch::async, [&] {
            attempting.set_value();
            const std::lock_guard operation(emucorev::savestate::save_state_operation_mutex());
            return true; // pointer read occurs only after mutex acquisition, as in JNI.
        });
        attempting.get_future().wait();
        check(jni.wait_for(80ms) == std::future_status::timeout, "cleanup excludes new JNI pointer admission");
        release.set_value(); cleanup.get(); check(jni.get(), "JNI admission resumes after cleanup");
    }
    {
        Session session;
        auto cleanup = std::async(std::launch::async, [&] { run_cleanup(session, true); });
        if (cleanup.wait_for(1s) != std::future_status::ready) {
            std::printf("FAIL startup-owned cleanup recursively deadlocked\n"); std::fflush(stdout); std::_Exit(1);
        }
        cleanup.get();
        check(session.steps.size() == 6, "startup failure reuses existing operation ownership");
    }
    std::printf("%s Android state cleanup serialization (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures != 0;
}
'''.replace('CLEANUP_BODY', cleanup)
(out / 'probe.cpp').write_text(cpp, encoding='utf-8')
if os.name == 'nt':
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler, 'C++ compiler required'
subprocess.run([compiler, '-std=c++20', '-pthread', '-I' + str(root / 'app/src/main/cpp/emucorev/include'), str(out / 'probe.cpp'), '-o', str(out / 'probe.exe')], check=True)
result = subprocess.run([str(out / 'probe.exe')], capture_output=True, text=True, timeout=5)
print(result.stdout + result.stderr)
(out / ('red.log' if result.returncode else 'green.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
raise SystemExit(result.returncode)
