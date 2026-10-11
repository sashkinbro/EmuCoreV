#!/usr/bin/env python3
"""Check real Android sensor dispatch excludes a concurrent state restore."""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
native = root / 'app/src/main/cpp'
android = (native / 'vita3k/vita3k/android/jni/main_android.cpp').read_text(encoding='utf-8')
start = 'case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:'
end = 'default:'
offset = android.index(start)
dispatch = android[offset:android.index(end, offset)]
out = root / 'app/build/motion-dispatch-probe'
out.mkdir(parents=True, exist_ok=True)
cpp = r'''
#include <emucorev/savestate/operation_mutex.h>
#include <SDL3/SDL_events.h>
#include <chrono>
#include <cstdio>
#include <future>
using namespace std::chrono_literals;
struct Env {};
std::promise<void>* entered = nullptr;
std::shared_future<void> continue_event;
template <typename Event> void handle_motion_event(Env&, int, const Event&) {
    entered->set_value(); continue_event.wait();
}
extern "C" SDL_SensorType SDL_GetSensorTypeForID(SDL_SensorID) { return SDL_SENSOR_GYRO; }
void dispatch(Env* emuenv, SDL_Event event) {
    switch (event.type) {
    PRODUCTION_DISPATCH
    default: break;
    }
}
int main() {
    int failed = 0;
    for (bool controller : {false, true}) {
        Env env; SDL_Event event{};
        event.type = controller ? SDL_EVENT_GAMEPAD_SENSOR_UPDATE : SDL_EVENT_SENSOR_UPDATE;
        std::promise<void> event_entered, resume_event;
        entered = &event_entered; continue_event = resume_event.get_future().share();
        auto handler = std::async(std::launch::async, [&] { dispatch(&env, event); });
        event_entered.get_future().wait();
        const bool admitted = emucorev::savestate::save_state_operation_mutex().try_lock();
        if (admitted) emucorev::savestate::save_state_operation_mutex().unlock();
        printf("%s active sensor owns operation mutex\n", admitted ? "FAIL" : "PASS");
        failed += admitted;
        std::promise<void> attempting;
        auto restore = std::async(std::launch::async, [&] {
            attempting.set_value();
            const std::lock_guard operation(emucorev::savestate::save_state_operation_mutex());
        });
        attempting.get_future().wait();
        const bool excluded = restore.wait_for(80ms) == std::future_status::timeout;
        printf("%s restore excludes active %s sensor dispatch\n", excluded ? "PASS" : "FAIL", controller ? "controller" : "device");
        failed += !excluded;
        resume_event.set_value(); handler.get(); restore.get();
    }
    return failed != 0;
}
'''.replace('PRODUCTION_DISPATCH', dispatch)
(out / 'probe.cpp').write_text(cpp, encoding='utf-8')
if os.name == 'nt':
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler
includes = [native / 'emucorev/include', native / 'vita3k/external/sdl/include']
subprocess.run([compiler, '-std=c++20', '-O0', *['-I'+str(p) for p in includes],
                str(out/'probe.cpp'), '-o', str(out/'probe.exe')], check=True)
result = subprocess.run([str(out/'probe.exe')], capture_output=True, text=True)
print(result.stdout, end='')
(out / 'result.log').write_text(result.stdout+result.stderr)
raise SystemExit(result.returncode)
