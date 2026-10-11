#!/usr/bin/env python3
"""Run the actual cold-launch boundary before window/renderer creation."""
from pathlib import Path
import os, shutil, subprocess
root=Path(__file__).resolve().parents[4]
out=root/'app/build/startup-gpu-config-probe';out.mkdir(parents=True,exist_ok=True)
source=(root/'app/src/main/cpp/vita3k/vita3k/Android/jni/main_android.cpp').read_text()
begin=source.index('        sync_current_game_id(launch_request.app_path);')
end=source.index('        SDL_WindowFlags window_flags = 0;',begin)
body=source[begin:end]
cpp=r'''
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#define LOG_ERROR(...) ((void)0)
namespace app {enum class AppSessionStopReason {LaunchFailure};}
struct Env {std::string mapping="disabled";float resolution=2;};
struct Request {std::string app_path="PCSE00333";};
struct Result {bool success=true;std::string error="invalid";bool ok()const{return success;}};
std::mutex operation;int prepared=0,cleanup=0,errors=0;bool invalid=false;
void sync_current_game_id(const std::string&){}
void report_save_state_load_error(const std::string&){++errors;}
namespace emucorev::savestate {
Result prepare_launch_gpu_config(Env& env,const std::string&){
 ++prepared;if(invalid)return {false,"invalid"};
 env.mapping="double-buffer";env.resolution=1;return {};
}
}
int failures=0,checks=0;
void check(bool value,const char* name){++checks;if(!value){++failures;std::printf("FAIL %s\n",name);}}
void run(const std::string& load_state_path){
 auto emuenv=std::make_unique<Env>();Request launch_request;int exit_code=0;
 std::unique_lock startup_state_operation(operation,std::defer_lock);
 auto cleanup_launch=[](app::AppSessionStopReason){++cleanup;};
 bool created=false;
 do {
''' + body + r'''
  created=true;
  check(load_state_path.empty()||(emuenv->mapping=="double-buffer"&&emuenv->resolution==1),"saved GPU settings applied before renderer creation");
 }while(false);
 if(invalid&&!load_state_path.empty())check(!created&&exit_code==-1,"invalid archive cannot reach renderer creation");
 else check(created,"valid cold launch creates renderer");
 if(load_state_path.empty())check(emuenv->mapping=="disabled"&&emuenv->resolution==2,"ordinary launch keeps configured GPU settings");
}
int main(){
 run("");check(prepared==0,"ordinary launch does not inspect an archive");
 run("slot.ecvs");check(prepared==1,"state inspected once at pre-renderer boundary");
 invalid=true;run("bad.ecvs");check(cleanup==1&&errors==1,"failed preparation reports and cleans launch exactly once");
 std::printf("Startup GPU override: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
'''
(out/'probe.cpp').write_text(cpp)
if os.name=='nt' and Path('C:/msys64/ucrt64/bin').is_dir():os.environ['PATH']='C:/msys64/ucrt64/bin;'+os.environ['PATH']
compiler=os.environ.get('CXX') or shutil.which('g++') or shutil.which('clang++');assert compiler
exe=out/'probe.exe';subprocess.run([compiler,'-std=c++20','-pthread',str(out/'probe.cpp'),'-o',str(exe)],check=True)
result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
print(result.stdout+result.stderr);(out/('red.log' if result.returncode else 'green.log')).write_text(result.stdout+result.stderr)
raise SystemExit(result.returncode)
