"""Execute the production launch method with guest-start side effects counted."""
from pathlib import Path
import os, shutil, subprocess

root = Path(__file__).resolve().parents[4]
source = (root / 'app/src/main/cpp/vita3k/vita3k/app/src/session_controller.cpp').read_text()
start = source.index('bool AppSessionController::load_and_run(')
end = source.index('\nbool AppSessionController::set_pause_reason', start)
method = source[start:end]
# Adapt only the old API signature so the unfixed implementation can execute
# the same behavioral regression instead of failing to compile.
method = method.replace('load_and_run()', 'load_and_run(bool restore_state)')
out = root / 'app/build/restore-launch-probe'
out.mkdir(parents=True, exist_ok=True)
cpp = r'''
#include <mutex>
#include <atomic>
#include <string>
#include <cstdio>
#define LOG_ERROR(...) ((void)0)
int loads=0, starts=0, syncs=0, renders=0;
bool load_ok=true, run_ok=true;
enum {Success};
struct Env {
 struct {void refresh_device_motion_support(){}} motion;
 struct {std::string title_id,app_path;} io;
 std::string self_name;
 struct R {void set_app(const char*,const char*){}} renderer_value;
 R* renderer=&renderer_value;
 int display=0,gxm=0,mem=0,cfg=0;
};
int load_app(int&,Env&,int){++loads;return load_ok?Success:1;}
int run_app(Env&,int,int){++starts;return run_ok?Success:1;}
void start_sync_thread(Env&){++syncs;}
void prepare_game_launch_overlay(Env&){}
namespace renderer {void start_render_thread(Env::R&,int&,int&,int&,int&){++renders;}}
struct Frame {void prepare_for_render_thread(){} void finalize_render_thread_start(){}};
struct FrameRef {Frame value; Frame& get(){return value;}};
enum class AppSessionPhase {Launching,Running};
const char* to_string(AppSessionPhase){return "phase";}
struct AppSessionController {
 Env emuenv;std::mutex mutex;
 std::atomic<AppSessionPhase> current_phase=AppSessionPhase::Launching;
 bool renderer_initialized=true,runtime_initialized=true;
 int active_launch_request=0;FrameRef frame_value;FrameRef* frame_host=&frame_value;
 bool load_and_run(bool restore_state=false);
 void set_phase(AppSessionPhase p){current_phase=p;}
};
'''
cpp += method + r'''
int main(){int failures=0;auto check=[&](bool ok,const char* label){std::printf("%s %s\n",ok?"PASS":"FAIL",label);failures+=!ok;};
 AppSessionController restore;
 check(restore.load_and_run(true),"restore initializes session");
 check(loads==1&&starts==0&&syncs==1&&renders==1,"restore never executes fresh guest module starts");
 AppSessionController normal;
 check(normal.load_and_run(),"ordinary launch succeeds");
 check(loads==2&&starts==1&&syncs==1&&renders==2,"ordinary guest startup retained");
 load_ok=false;AppSessionController bad;
 check(!bad.load_and_run(true)&&starts==1&&renders==2&&syncs==1,"failed load cannot start producers");
 return failures?1:0;
}
'''
(out / 'probe.cpp').write_text(cpp)
if os.name == 'nt':
 os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
compiler = shutil.which('g++') or shutil.which('clang++')
assert compiler
exe = out / 'probe.exe'
subprocess.run([compiler,'-std=c++20',str(out/'probe.cpp'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True,timeout=10)
