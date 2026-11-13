#!/usr/bin/env python3
"""Run actual NP module bodies and state with host-only guest-call/IO adapters.

No Android/Gradle/CMake/ADB. Each case has its own process so a bad iterator,
null dereference or deadlock is a reported regression instead of hiding others.
"""
from pathlib import Path
import argparse
import os
import re
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--source-root', type=Path)
args = parser.parse_args()
ROOT = next(p for p in Path(__file__).resolve().parents if (p / 'app/src/main/cpp/vita3k/vita3k').is_dir())
BASE = args.source_root or ROOT / 'app/src/main/cpp/vita3k/vita3k'
OUT = ROOT / 'tools/system-services/host-build'
OUT.mkdir(parents=True, exist_ok=True)

def source(path):
    return re.sub(r'^#(?:include[^\n]*|pragma once)', '', (BASE / path).read_text(encoding='utf-8'), flags=re.M)

cpp = r'''
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
using Address=uint32_t; using SceUID=int32_t; using SceUInt32=uint32_t;
using SceUInt16=uint16_t; using SceUChar8=uint8_t; using SceInt8=int8_t;
using SceInt=int32_t;
template<class T> struct Ptr {Address addr=0; explicit operator bool() const{return addr!=0;}
 Address address()const{return addr;} bool valid(int)const{return addr!=0;}
 T* get(int)const{return reinterpret_cast<T*>(uintptr_t(addr));}};
struct IOState {std::string user_name="LocalPlayer";};
namespace np::trophy {enum class SceNpTrophyGrade{};
struct Context {bool valid=true;IOState* io=nullptr;int trophy_file_stream=0;};}
int close_count=0;
void close_file(IOState&,int,const char*){++close_count;}
#define TRACY_MODULE_NAME(...)
#define TRACY_FUNC(...)
#define LOG_ERROR(...)
#define LOG_WARN(...)
#define RET_ERROR(x) (static_cast<int>(x))
#define STUBBED(...) 0
#define UNIMPLEMENTED() 0
'''+source('np/include/np/common.h')+source('np/include/np/state.h')+r'''
struct GuestThread {std::vector<std::pair<Address,std::vector<uint32_t>>> calls;
 std::function<void(Address)> hook;
 uint32_t run_callback(Address pc,const std::vector<uint32_t>& args){calls.push_back({pc,args});if(hook)hook(pc);return 0;}};
using ThreadStatePtr=std::shared_ptr<GuestThread>;
struct EmuEnvState {NpState np;IOState io;int mem=0;
 struct {struct {bool psn_signed_in=false;}current_config;}cfg;
 struct {std::mutex mutex;int next=0;ThreadStatePtr thread=std::make_shared<GuestThread>();
  int get_next_uid(){return ++next;} ThreadStatePtr get_thread(int){return thread;}}kernel;};
#define EXPORT(ret,name,...) ret export_##name(EmuEnvState &emuenv,int thread_id,const char *export_name,##__VA_ARGS__)
#define CALL_EXPORT(name,...) export_##name(emuenv,thread_id,#name,##__VA_ARGS__)
#define CALL(env,name,...) export_##name(env,1,#name,##__VA_ARGS__)
bool init(NpTrophyState&);bool deinit(NpTrophyState&);
EXPORT(int,sceNpManagerGetNpId,np::SceNpId *id);
'''+source('np/src/init.cpp')+source('modules/SceNpManager/SceNpManager.cpp')+source('modules/SceNpMatching2/SceNpMatching2.cpp')+r'''
int main(int argc,char**argv){if(argc!=2)return 2;std::string name=argv[1];EmuEnvState env;int fails=0;
 auto check=[&](bool ok,const char* what){printf("%s %s\n",ok?"PASS":"FAIL",what);fails+=!ok;};
 auto start=[&]{if(CALL(env,sceNpInit,nullptr,nullptr)!=0)check(false,"NP init succeeds locally");};
 auto reg=[&](Address pc){return CALL(env,sceNpRegisterServiceStateCallback,Ptr<void>{pc},Ptr<void>{77});};
 auto queue=[&](uint32_t ctx){return CALL(env,sceNpMatching2ContextStart,ctx,0,0,0);};
 auto matching_reg=[&](Address pc){return CALL(env,sceNpMatching2RegisterContextCallback,Ptr<void>{pc},Ptr<void>{88});};
 if(name=="uninitialized_state"){
  SceNpServiceState state=SCE_NP_SERVICE_STATE_UNKNOWN;
  check(uint32_t(CALL(env,sceNpGetServiceState,&state))==SCE_NP_ERROR_NOT_INITIALIZED,"state before init returns NP not initialized");
  check(state==SCE_NP_SERVICE_STATE_UNKNOWN,"failed state query preserves output");
  check(uint32_t(CALL(env,sceNpCheckCallback))==SCE_NP_ERROR_NOT_INITIALIZED,"callback pump before init returns NP not initialized");
 }else if(name=="null_state"){
  start();check(uint32_t(CALL(env,sceNpGetServiceState,nullptr))==SCE_NP_ERROR_INVALID_ARGUMENT,"null state rejected");
 }else if(name=="registration_validation"){
  check(uint32_t(reg(100))==SCE_NP_ERROR_NOT_INITIALIZED,"registration before init rejected");
  start();check(uint32_t(reg(0))==SCE_NP_ERROR_INVALID_ARGUMENT,"null callback rejected");
  check(env.np.cbs.empty(),"invalid registration leaves no callback");
 }else if(name=="state_transitions"){
  start();reg(100);CALL(env,sceNpCheckCallback);CALL(env,sceNpCheckCallback);
  check(env.kernel.thread->calls.size()==1,"unchanged local state delivered once");
  check(env.kernel.thread->calls[0].second==std::vector<uint32_t>({1,0,77}),"signed-out callback state/result/user data");
  env.cfg.current_config.psn_signed_in=true;CALL(env,sceNpCheckCallback);
  check(env.kernel.thread->calls.size()==2&&env.kernel.thread->calls.back().second[0]==2,"configured offline signed-in transition delivered");
  SceNpServiceState state{};check(CALL(env,sceNpGetServiceState,&state)==0&&state==SCE_NP_SERVICE_STATE_SIGNED_IN,"offline sign-in never reports ONLINE");
 }else if(name=="self_unregister"){
  start();reg(100);env.kernel.thread->hook=[&](Address){CALL(env,sceNpUnregisterServiceStateCallback);};
  CALL(env,sceNpCheckCallback);CALL(env,sceNpCheckCallback);
  check(env.kernel.thread->calls.size()==1&&env.np.cbs.empty(),"callback unregisters itself safely");
 }else if(name=="callback_term"){
  start();reg(100);reg(200);env.kernel.thread->hook=[&](Address){CALL(env,sceNpTerm);};
  CALL(env,sceNpCheckCallback);
  check(env.kernel.thread->calls.size()==1&&!env.np.inited,"termination inside callback cancels remaining dispatch");
 }else if(name=="nested_pump"){
  start();reg(100);env.kernel.thread->hook=[&](Address){CALL(env,sceNpCheckCallback);};
  CALL(env,sceNpCheckCallback);check(env.kernel.thread->calls.size()==1,"nested pump does not redeliver unchanged service event");
 }else if(name=="term_clears_matching"){
  start();matching_reg(300);queue(5);CALL(env,sceNpTerm);
  check(env.np.matching2.pending.empty()&&env.np.matching2.context_cb_pc==0&&env.np.matching2.context_cb_arg==0,"NP term clears matching events and callback addresses");
  start();CALL(env,sceNpCheckCallback);check(env.kernel.thread->calls.empty(),"new NP lifecycle cannot call old matching callback");
 }else if(name=="matching_replacement"){
  start();matching_reg(300);queue(5);matching_reg(400);CALL(env,sceNpCheckCallback);
  check(env.kernel.thread->calls.empty(),"queued matching event cannot cross callback registration");
 }else if(name=="matching_term_during_dispatch"){
  start();matching_reg(300);queue(5);queue(6);
  env.kernel.thread->hook=[&](Address){CALL(env,sceNpMatching2Term);};
  CALL(env,sceNpCheckCallback);check(env.kernel.thread->calls.size()==1,"matching term in callback cancels detached events");
 }else if(name=="matching_rebind_same_address"){
  start();matching_reg(300);queue(5);queue(6);
  env.kernel.thread->hook=[&](Address){matching_reg(300);};
  CALL(env,sceNpCheckCallback);check(env.kernel.thread->calls.size()==1,"same-address matching rebind cancels detached generation");
 }else if(name=="matching_offline_event"){
  start();matching_reg(300);queue(5);CALL(env,sceNpCheckCallback);CALL(env,sceNpCheckCallback);
  check(env.kernel.thread->calls.size()==1,"matching offline failure delivered once");
  check(env.kernel.thread->calls[0].second==std::vector<uint32_t>({5,2,0,0x80410123,88}),"matching failure preserves existing ABI and honest no-server result");
 }else if(name=="matching_nested_order"){
  start();matching_reg(300);queue(5);queue(6);
  env.kernel.thread->hook=[&](Address){if(env.kernel.thread->calls.size()==1){queue(7);CALL(env,sceNpCheckCallback);}};
  CALL(env,sceNpCheckCallback);CALL(env,sceNpCheckCallback);
  std::vector<uint32_t> ids;for(auto&call:env.kernel.thread->calls)ids.push_back(call.second[0]);
  check(ids==std::vector<uint32_t>({5,6,7}),"nested pump cannot overtake detached matching events");
 }else if(name=="callback_unregister_other"){
  start();reg(100);reg(200);env.kernel.thread->hook=[&](Address){CALL(env,sceNpUnregisterServiceStateCallback);};
  CALL(env,sceNpCheckCallback);check(env.kernel.thread->calls.size()==1,"unregistered callback in snapshot is skipped");
 }else if(name=="callback_new_session"){
  start();reg(100);reg(200);
  env.kernel.thread->hook=[&](Address){CALL(env,sceNpTerm);start();matching_reg(300);queue(9);reg(400);};
  CALL(env,sceNpCheckCallback);check(env.kernel.thread->calls.size()==1,"old pump stops after term and reinit");
  env.kernel.thread->hook={};CALL(env,sceNpCheckCallback);
  check(env.kernel.thread->calls.size()==3&&env.kernel.thread->calls[1].first==400&&env.kernel.thread->calls[2].second[0]==9,"new session callbacks wait for next pump");
 }else if(name=="matching_new_session"){
  start();matching_reg(300);queue(5);queue(6);
  env.kernel.thread->hook=[&](Address){CALL(env,sceNpTerm);start();matching_reg(300);queue(9);};
  CALL(env,sceNpCheckCallback);check(env.kernel.thread->calls.size()==1,"old matching pump stops across NP term and reinit");
  env.kernel.thread->hook={};CALL(env,sceNpCheckCallback);
  check(env.kernel.thread->calls.size()==2&&env.kernel.thread->calls.back().second[0]==9,"new matching lifecycle survives cancellation of old pump");
 }else if(name=="repeated_lifecycle"){
  for(int i=0;i<50;++i){start();reg(100);matching_reg(300);queue(i+1);CALL(env,sceNpCheckCallback);CALL(env,sceNpTerm);CALL(env,sceNpTerm);}
  check(env.kernel.thread->calls.size()==100&&env.np.cbs.empty()&&env.np.state_cb_id==0&&env.np.matching2.pending.empty(),"repeated init/term leaves no callbacks or pending events");
 }else if(name=="duplicate_init"){
  start();reg(100);check(uint32_t(CALL(env,sceNpInit,nullptr,nullptr))==SCE_NP_ERROR_ALREADY_INITIALIZED,"duplicate NP init rejected");
  CALL(env,sceNpCheckCallback);check(env.kernel.thread->calls.size()==1,"duplicate init preserves existing session");
 }else if(name=="session_cleanup_without_np_init"){
  init(env.np.trophy_state);env.np.trophy_state.contexts.push_back({true,&env.io,45});
  matching_reg(300);queue(5);deinit(env.np);
  check(!env.np.trophy_state.inited&&env.np.trophy_state.contexts.empty()&&close_count==1,"app teardown closes independently initialized trophy context without NP init");
  check(env.np.matching2.pending.empty()&&env.np.matching2.context_cb_pc==0,"app teardown clears matching state without NP init");
 }else if(name=="null_profile"){
  check(uint32_t(CALL(env,sceNpManagerGetNpId,nullptr))==SCE_NP_MANAGER_ERROR_INVALID_ARGUMENT,"null NP ID output rejected");
 }else{return 2;}
 return fails?1:0;
}
'''
(OUT / 'np_probe.cpp').write_text(cpp, encoding='utf-8')
compiler_name = os.environ.get('CXX') or shutil.which('g++')
if not compiler_name and os.name == 'nt':
    compiler_name = 'C:/msys64/ucrt64/bin/g++.exe'
assert compiler_name, 'Set CXX to a host g++ compiler (C++20 and debug iterators required)'
compiler = Path(compiler_name)
env = os.environ.copy()
env['PATH'] = str(compiler.parent) + os.pathsep + env['PATH']
subprocess.run([str(compiler), '-std=c++20', '-O0', '-g', '-D_GLIBCXX_DEBUG', '-pthread',
                str(OUT / 'np_probe.cpp'), '-o', str(OUT / 'np_probe.exe')], check=True, env=env)
cases = ['uninitialized_state', 'null_state', 'registration_validation', 'state_transitions',
         'self_unregister', 'callback_term', 'nested_pump', 'term_clears_matching',
         'matching_replacement', 'matching_term_during_dispatch', 'matching_rebind_same_address',
         'matching_offline_event', 'matching_nested_order', 'callback_unregister_other',
         'callback_new_session', 'matching_new_session', 'repeated_lifecycle', 'duplicate_init',
         'session_cleanup_without_np_init', 'null_profile']
failed = 0
results = []
for case in cases:
    try:
        result = subprocess.run([str(OUT / 'np_probe.exe'), case], env=env, capture_output=True, text=True, timeout=8)
        ok = result.returncode == 0
        detail = result.stdout + result.stderr
        if not ok:
            detail += f'Process exit code: {result.returncode}\n'
    except subprocess.TimeoutExpired:
        ok = False
        detail = 'TIMEOUT (callback deadlock or recursion)\n'
    failed += not ok
    results.append(f"{'PASS' if ok else 'FAIL'} CASE {case}\n{detail}")
    print(results[-1], end='')
print(f'{len(cases)-failed}/{len(cases)} cases passed')
(OUT / 'results.log').write_text('\n'.join(results)+f'\n{len(cases)-failed}/{len(cases)} cases passed\n', encoding='utf-8')
raise SystemExit(bool(failed))
