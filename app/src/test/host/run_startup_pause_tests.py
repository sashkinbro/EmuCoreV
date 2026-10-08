#!/usr/bin/env python3
"""Run the real startup Load block, shared pause scope and controller pause methods.

Only the environment/audio/kernel side effects and actual Load implementation are
host probes: no firmware/device/Vulkan is required. Extraction asserts unique
production markers so source refactors fail visibly instead of testing stale code.
Run from any directory: python app/src/test/host/run_startup_pause_tests.py
"""
from pathlib import Path
import subprocess,os,shutil
if os.name == "nt" and Path("C:/msys64/ucrt64/bin").is_dir():
 os.environ["PATH"]="C:/msys64/ucrt64/bin;"+os.environ["PATH"]
root=Path(__file__).resolve().parents[4];out=root/'app/build/startup-pause-probe';out.mkdir(parents=True,exist_ok=True)
def block(s,marker):
 assert s.count(marker)==1, f'Expected exactly one production marker: {marker}'
 p=s.index(marker);a=s.index('{',p);depth=0
 for i in range(a,len(s)):
  depth+=(s[i]=='{')-(s[i]=='}')
  if depth==0:return s[p:i+1]
 raise AssertionError(f'Unterminated production block: {marker}')
controller=(root/'app/src/main/cpp/vita3k/vita3k/app/src/session_controller.cpp').read_text()
android=(root/'app/src/main/cpp/vita3k/vita3k/android/jni/main_android.cpp').read_text()
bridge=(root/'app/src/main/cpp/emucorev/src/savestate_bridge.cpp').read_text()
startup=block(android,'        if (!load_state_path.empty()) {')
guard=block((root/'app/src/main/cpp/emucorev/include/emucorev/savestate/session_pause.h').read_text(),'class ScopedSaveStatePause')+';'
cpp=r'''
#include <atomic>
#include <mutex>
#include <string>
#include <utility>
#include <cstdio>
#include <cstdint>
#define LOG_INFO(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
struct Kernel {bool paused=false; bool is_threads_paused(){return paused;}void pause_threads(){paused=true;}void resume_threads(){paused=false;}};
struct Audio {bool adapter=true,paused=false,new_port_started=false;int calls=0;void switch_state(bool p){++calls;paused=p;if(!p)new_port_started=true;}};
struct Env {Kernel kernel;Audio audio;bool drop_inputs=false;struct {std::atomic<bool> overlay_input_intercepted=false;}ctrl;struct R {std::atomic<bool> paused=false;} renderer_value;R *renderer=&renderer_value;};
namespace app {
enum class AppSessionPhase {Idle,Launching,Running,Stopping};
enum class AppSessionPauseReason:uint32_t {None=0,Menu=2,Background=4,SaveState=8};
enum class AppSessionStopReason {LaunchFailure};
uint32_t to_pause_mask(AppSessionPauseReason p){return uint32_t(p);}
struct AppSessionController {Env &emuenv;std::mutex mutex;std::atomic<AppSessionPhase> current_phase=AppSessionPhase::Running;std::atomic<uint32_t> active_pause_reasons=0;bool input_intercepted=false;bool set_pause_reason(AppSessionPauseReason,bool);void apply_runtime_state_locked();};
'''
cpp+=block(controller,'bool AppSessionController::set_pause_reason')+'\n'+block(controller,'void AppSessionController::apply_runtime_state_locked')+'\n}\n'
cpp+='app::AppSessionController *global_controller=nullptr;auto *get_app_session_controller(){return global_controller;}\nnamespace emucorev::savestate { '+guard+' }\nusing emucorev::savestate::ScopedSaveStatePause;\n'
cpp+=r'''
bool restore_paused=false,restore_ok=true;int cleanup_count=0,cleanup_audio_calls=0,restore_calls=0;
struct Result {std::string error="deliberate restore failure";bool ok()const{return restore_ok;}};
namespace emucorev::savestate { Result load_state(Env &env,const std::string &,bool){++restore_calls;restore_paused=env.kernel.paused&&env.audio.paused;env.audio.new_port_started=false;return {};} }
void report_save_state_load_error(const std::string &){}
void startup(Env *emuenv,app::AppSessionController *session_controller) {
std::string load_state_path="fixture";int exit_code=0;
auto cleanup_launch=[&](app::AppSessionStopReason){++cleanup_count;cleanup_audio_calls=emuenv->audio.calls;session_controller->current_phase=app::AppSessionPhase::Idle;emuenv->audio.adapter=false;};
do {
'''+startup+r'''
}while(false);
}
int main(){int failed=0;auto check=[&](bool ok,const char *name){std::printf("%s %s\n",ok?"PASS":"FAIL",name);failed+=!ok;};
Env env;app::AppSessionController c{env};global_controller=&c;
startup(&env,&c);check(restore_paused,"startup restore is inside kernel/audio pause");check(env.audio.new_port_started,"startup restored stopped audio port resumes");
restore_paused=false;{ScopedSaveStatePause pause(get_app_session_controller());check(bool(pause),"JNI pause acquired");emucorev::savestate::load_state(env,"fixture",true);}check(restore_paused&&env.audio.new_port_started,"JNI restore paused and resumed");
c.set_pause_reason(app::AppSessionPauseReason::Menu,true);c.set_pause_reason(app::AppSessionPauseReason::Background,true);startup(&env,&c);check(env.kernel.paused&&env.audio.paused&&!env.audio.new_port_started&&c.active_pause_reasons==6,"startup preserves Menu and Background reasons");
c.set_pause_reason(app::AppSessionPauseReason::Menu,false);c.set_pause_reason(app::AppSessionPauseReason::Background,false);restore_ok=false;startup(&env,&c);const int calls=cleanup_audio_calls;check(cleanup_count==1&&c.current_phase==app::AppSessionPhase::Idle,"failed restore cleans stopped session");check(env.audio.calls==calls,"no resume after cleanup");
const int old_restore_calls=restore_calls;startup(&env,&c);check(restore_calls==old_restore_calls,"inactive startup cannot invoke restore");
{ScopedSaveStatePause pause(nullptr);check(!pause,"null controller cannot acquire pause");}
return failed?1:0;}
'''
(out/'probe.cpp').write_text(cpp)
gpp=os.environ.get('CXX') or shutil.which('g++');assert gpp, 'A host C++20 compiler (CXX or g++) is required';subprocess.run([gpp,'-std=c++20','-O0','-pthread',str(out/'probe.cpp'),'-o',str(out/'probe.exe')],check=True)
r=subprocess.run([str(out/'probe.exe')],capture_output=True,text=True);print(r.stdout);(out/'result.log').write_text(r.stdout+r.stderr);print(r.stderr,end='');raise SystemExit(r.returncode)
