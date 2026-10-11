#!/usr/bin/env python3
"""Exercise production pause application during bounded audio callback drain."""
from pathlib import Path
import os, shutil, subprocess

root = Path(__file__).resolve().parents[4]
out = root / 'app/build/session-drain-probe'
out.mkdir(parents=True, exist_ok=True)
if os.name == 'nt':
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
source = (root / 'app/src/main/cpp/vita3k/vita3k/app/src/session_controller.cpp').read_text()

def block(marker, text=source):
    assert text.count(marker) == 1
    start = text.index(marker)
    brace = text.index('{', start)
    depth = 0
    for end in range(brace, len(text)):
        depth += (text[end] == '{') - (text[end] == '}')
        if depth == 0:
            return text[start:end + 1]
    raise AssertionError(marker)

cpp = r'''
#include <atomic>
#include <mutex>
#include <cstdio>
#include <cstdint>
struct Env {
 struct {bool paused=true;bool is_threads_paused(){return paused;}void pause_threads(){paused=true;}void resume_threads(){paused=false;}} kernel;
 struct {bool adapter=true,paused=true;void switch_state(bool value){paused=value;}} audio;
 bool drop_inputs=false;
 struct {std::atomic<bool> overlay_input_intercepted=false;} ctrl;
 struct Renderer {std::atomic<bool> paused=false;} renderer_value;
 Renderer *renderer=&renderer_value;
};
namespace app {
enum class AppSessionPauseReason:uint32_t {User=1,Menu=2,Background=4,SaveState=8};
enum class AppSessionPhase {Idle,Running,Stopping};
uint32_t to_pause_mask(AppSessionPauseReason value){return uint32_t(value);}
struct AppSessionController {
 Env &emuenv;std::atomic<uint32_t> active_pause_reasons=2;
 std::mutex mutex;std::atomic<AppSessionPhase> current_phase=AppSessionPhase::Running;
 bool input_intercepted=false,save_state_draining=false;
 void apply_runtime_state_locked();
 bool set_save_state_draining(bool);
};
''' + block('void AppSessionController::apply_runtime_state_locked') + '\n' + block('bool AppSessionController::set_save_state_draining') + '\n}\n'
pause_header = (root / 'app/src/main/cpp/emucorev/include/emucorev/savestate/session_pause.h').read_text()
cpp += 'namespace emucorev::savestate { ' + block('class ScopedSaveStateDrain', pause_header) + '; }\n' + r'''
int main(){
 int failed=0;auto check=[&](bool value,const char *name){printf("%s %s\n",value?"PASS":"FAIL",name);failed+=!value;};
 Env env;app::AppSessionController controller{env};
 controller.save_state_draining=true;controller.apply_runtime_state_locked();
 check(!env.kernel.paused,"menu-paused callback can finish during drain");
 check(env.audio.paused&&env.renderer->paused&&env.drop_inputs&&env.ctrl.overlay_input_intercepted,"drain preserves presentation, audio and input pause");
 check(controller.active_pause_reasons==2,"drain preserves menu reason ownership");
 controller.active_pause_reasons=1;controller.apply_runtime_state_locked();
 check(!env.kernel.paused,"explicit user pause also permits bounded drain");
 controller.active_pause_reasons=6;controller.apply_runtime_state_locked();
 check(env.kernel.paused,"background pause prevents callback execution");
 controller.active_pause_reasons=10;controller.apply_runtime_state_locked();
 check(env.kernel.paused,"capture pause ends callback execution before snapshot");
 controller.active_pause_reasons=2;controller.save_state_draining=false;controller.apply_runtime_state_locked();
 check(env.kernel.paused&&env.audio.paused&&controller.active_pause_reasons==2,"failed drain restores menu pause");
 controller.active_pause_reasons=0;controller.save_state_draining=true;controller.apply_runtime_state_locked();
 check(!env.kernel.paused&&env.audio.paused&&env.renderer->paused&&env.drop_inputs,"running session drains without accepting input or presenting frames");
 controller.save_state_draining=false;controller.apply_runtime_state_locked();
 check(!env.kernel.paused&&!env.audio.paused&&!env.renderer->paused&&!env.drop_inputs,"running session resumes after drain ends");
 controller.active_pause_reasons=2;controller.apply_runtime_state_locked();
 {emucorev::savestate::ScopedSaveStateDrain drain(&controller);
  check(bool(drain)&&!env.kernel.paused,"RAII starts callback drain from menu pause");
  emucorev::savestate::ScopedSaveStateDrain competing(&controller);
  check(!competing&&controller.save_state_draining,"competing drain cannot take ownership");
 }
 check(env.kernel.paused&&!controller.save_state_draining,"RAII restores menu pause on early return");
 controller.active_pause_reasons=6;controller.apply_runtime_state_locked();
 {emucorev::savestate::ScopedSaveStateDrain drain(&controller);check(!drain&&env.kernel.paused,"background session rejects new drain");}
 controller.active_pause_reasons=10;controller.apply_runtime_state_locked();
 {emucorev::savestate::ScopedSaveStateDrain drain(&controller);check(!drain&&env.kernel.paused,"another capture rejects new drain");}
 controller.current_phase=app::AppSessionPhase::Stopping;
 {emucorev::savestate::ScopedSaveStateDrain drain(&controller);check(!drain,"stopping session rejects new drain");}
 {emucorev::savestate::ScopedSaveStateDrain drain(nullptr);check(!drain,"absent session rejects new drain");}
 return failed?1:0;
}
'''
(out / 'probe.cpp').write_text(cpp)
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler
subprocess.run([compiler, '-std=c++20', '-pthread', str(out / 'probe.cpp'), '-o', str(out / 'probe.exe')], check=True)
raise SystemExit(subprocess.run([str(out / 'probe.exe')]).returncode)
