#!/usr/bin/env python3
"""Exercise production SDL dispatch and contact release with a restore latch."""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
native = root / 'app/src/main/cpp'
android = (native / 'vita3k/vita3k/android/jni/main_android.cpp').read_text(encoding='utf-8')
touch = (native / 'vita3k/vita3k/touch/src/touch.cpp').read_text(encoding='utf-8')

def block(source, marker):
    assert source.count(marker) == 1, 'production marker must be unique: ' + marker
    start = source.index(marker)
    brace = source.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        assert end < len(source)
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

finger = block(touch, 'int handle_touch_event(')
pad = block(touch, 'int handle_touchpad_event(')
for body, decrement in ((finger, '--state.finger_count;'), (pad, '--state.touchpad_finger_count;')):
    assert body.count(decrement) == 1, 'review release synchronization seam after refactor'
finger = finger.replace('--state.finger_count;', 'before_release(); --state.finger_count;')
pad = pad.replace('--state.touchpad_finger_count;', 'before_release(); --state.touchpad_finger_count;')
finger_case = block(android, 'case SDL_EVENT_FINGER_CANCELED: {')
pad_start = 'case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:'
pad_end = 'case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:'
assert android.count(pad_start) == android.count(pad_end) == 1
pad_case = android[android.index(pad_start):android.index(pad_end)]

out = root / 'app/build/touch-restore-dispatch-probe'
out.mkdir(parents=True, exist_ok=True)
cpp = r'''
#include <emucorev/savestate/operation_mutex.h>
#include <touch/state.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <future>
using namespace std::chrono_literals;
thread_local std::promise<void>* release_entered = nullptr;
thread_local std::shared_future<void> release_continue;
void before_release() {
    if (release_entered) { release_entered->set_value(); release_continue.wait(); }
}
PRODUCTION_HANDLERS
struct Env {
    TouchState touch;
    struct { struct { std::atomic<float> x{0}, y{0}; std::atomic<bool> pressed{false}; } overlay_mouse; } ctrl;
};
void dispatch(Env* emuenv, SDL_Event event) {
    switch(event.type) {
    FINGER_CASE
    PAD_CASE
    default: break;
    }
}
int main() {
    int failed = 0;
    auto check = [&](bool ok, const char* label) { if (!ok) { ++failed; std::printf("FAIL %s\n", label); } };
    for (bool touchpad : {false, true}) {
        Env env;
        SDL_Event event{};
        if (touchpad) {
            env.touch.touchpad_finger_count = 1;
            env.touch.touchpad_buffer[0].finger = 4;
            event.gtouchpad.type = SDL_EVENT_GAMEPAD_TOUCHPAD_UP;
            event.gtouchpad.finger = 4;
        } else {
            env.touch.finger_count = 1;
            env.touch.finger_buffer[0].fingerID = 4;
            event.tfinger.type = SDL_EVENT_FINGER_CANCELED;
            event.tfinger.fingerID = 4;
        }
        std::promise<void> entered, continue_release;
        auto resume = continue_release.get_future().share();
        auto handler = std::async(std::launch::async, [&] {
            release_entered = &entered; release_continue = resume;
            dispatch(&env, event);
        });
        entered.get_future().wait();
        std::promise<void> attempting;
        auto restore = std::async(std::launch::async, [&] {
            attempting.set_value();
            // JNI/startup read_input already owns this production operation lock.
            const std::lock_guard operation(emucorev::savestate::save_state_operation_mutex());
            env.touch.finger_count = 0; env.touch.touchpad_finger_count = 0;
            std::fill_n(env.touch.finger_buffer, 8, SDL_TouchFingerEvent{});
            std::fill_n(env.touch.touchpad_buffer, 8, SDL_GamepadTouchpadEvent{});
        });
        attempting.get_future().wait();
        check(restore.wait_for(80ms) == std::future_status::timeout,
            touchpad ? "restore excludes active touchpad release" : "restore excludes active finger release");
        continue_release.set_value(); handler.get(); restore.get();
        check(env.touch.finger_count == 0 && env.touch.touchpad_finger_count == 0,
            touchpad ? "touchpad release cannot underflow cleared count to255" : "finger release cannot underflow cleared count to255");
        // Keep the RED probe safe after recording the underflow: do not let
        // another handler walk 255 contacts in an eight-element array.
        env.touch.finger_count = 0; env.touch.touchpad_finger_count = 0;
        // A queued late UP after restoration remains a harmless no-contact event.
        dispatch(&env, event);
        check(env.touch.finger_count == 0 && env.touch.touchpad_finger_count == 0, "late release after restore remains neutral");
    }
    std::printf("%s touch restore/dispatch serialization (%d failures)\n", failed ? "FAIL" : "PASS", failed);
    return failed != 0;
}
'''.replace('PRODUCTION_HANDLERS', finger + '\n' + pad).replace('FINGER_CASE', finger_case).replace('PAD_CASE', pad_case)
(out / 'probe.cpp').write_text(cpp, encoding='utf-8')
if os.name == 'nt':
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler
includes = [native / 'emucorev/include', native / 'vita3k/vita3k/touch/include', native / 'vita3k/vita3k/util/include', native / 'vita3k/external/sdl/include']
subprocess.run([compiler, '-std=c++20', '-pthread', *['-I'+str(path) for path in includes], str(out / 'probe.cpp'), '-o', str(out / 'probe.exe')], check=True)
result = subprocess.run([str(out / 'probe.exe')], capture_output=True, text=True, timeout=5)
print(result.stdout + result.stderr)
(out / ('red.log' if result.returncode else 'green.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
raise SystemExit(result.returncode)
