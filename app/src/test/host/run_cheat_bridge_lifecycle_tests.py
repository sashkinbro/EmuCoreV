#!/usr/bin/env python3
"""Run real cheat interpreter and JNI delete body with host memory/session seams.

File lookup/parsing and JNI are narrow seams; B200 interpretation, writes,
saved ARM restoration, pack clearing and bridge lock acquisition are production.
"""
from pathlib import Path
import os, shutil, subprocess, sys

root=Path(__file__).resolve().parents[4]
core=root/'app/src/main/cpp/vita3k/vita3k'
out=root/'app/build/cheat-bridge-lifecycle-probe';out.mkdir(parents=True,exist_ok=True)
bridge_path='app/src/main/cpp/emucorev/src/cheat_bridge.cpp'
bridge=(root/bridge_path).read_text()
interpreter=(core/'cheat/src/cheat.cpp').read_text()
if '--baseline' in sys.argv:
    bridge=subprocess.check_output(['git','show','HEAD:'+bridge_path],cwd=root,text=True)
    interpreter=subprocess.check_output(['git','show','HEAD:app/src/main/cpp/vita3k/vita3k/cheat/src/cheat.cpp'],cwd=root,text=True)
def block(s,marker):
    assert s.count(marker)==1,marker
    a=s.index(marker);b=s.index('{',a);depth=0
    for i in range(b,len(s)):
        depth+=(s[i]=='{')-(s[i]=='}')
        if not depth:return s[a:i+1]
    raise AssertionError(marker)

support=out/'include';(support/'util').mkdir(parents=True,exist_ok=True)
(support/'util/fs.h').write_text('#pragma once\n#include <filesystem>\nnamespace fs=std::filesystem;\n')
cpp=r'''
#include <cheat/functions.h>
#include <emucorev/savestate/operation_mutex.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <future>
#include <thread>
constexpr uint32_t KiB(uint32_t n){return n*1024;}
struct MemState {std::array<uint8_t,65536> bytes{};bool use_page_table=false;std::array<uint8_t*,16> page_table{};};
bool is_valid_addr_range(const MemState&,uint32_t a,uint32_t b){return a<b&&b<=65536;}
template<class T> struct Ptr {uint32_t addr;explicit Ptr(uint32_t v):addr(v){} T* get(const MemState& m)const{return (T*)&m.bytes[addr];}};
#define LOG_WARN_ONCE(...) ((void)0)
#define LOG_DEBUG(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
#define LOG_INFO(...) ((void)0)
namespace cheat {
fs::path find_cheat_file(const fs::path& dir,const std::string& id){auto p=dir/(id+".psv");return fs::exists(p)?p:fs::path{};}
CheatFile parse_cheat_file(const fs::path& path,const std::string& id){
 CheatFile f;f.path=path;f.title_id=id;
 Cheat c;c.name="inf.HP";c.enabled_on_boot=true;c.lines={{0xB200,1,0},{0x0200,0x10,0x20000}};f.cheats.push_back(c);return f;
}
bool save_cheat_file(const CheatFile&){return true;}
}
'''
cpp+='\n'.join(line for line in interpreter.splitlines() if not line.startswith('#include'))+'\n'
cpp+=r'''
using jboolean=bool;using jstring=std::string;using jobject=int;struct JNIEnv{};
constexpr bool JNI_FALSE=false,JNI_TRUE=true;
#define JNIEXPORT
#define JNICALL
struct EmuEnvState {CheatState cheat;MemState mem;fs::path directory;};
EmuEnvState* current=nullptr;
EmuEnvState* get_emuenv(){return current;}
std::string jstring_to_string(JNIEnv*,jstring s){return s;}
bool is_live_title(EmuEnvState*,const std::string&){return true;}
fs::path cheats_directory(const EmuEnvState& env){return env.directory;}
namespace fs_utils {std::string path_to_utf8(const fs::path& p){return p.string();}}
cheat::JitInvalidate make_invalidate(EmuEnvState&){return {};}
'''
# jstring is a pointer in JNI; keep the body unchanged by supplying a pointer seam.
cpp=cpp.replace('using jstring=std::string;', 'using jstring=const char*;').replace('return s;}','return s?s:"";}')
cpp+=block(bridge,'extern "C" JNIEXPORT jboolean JNICALL\nJava_com_sbro_emucorev_core_CheatBridge_deleteCheats(')+'\n'
cpp+=r'''
int failures=0,checks=0;
void check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
uint32_t read(const MemState& m,uint32_t a){uint32_t x;std::memcpy(&x,m.bytes.data()+a,4);return x;}
int main(){
 EmuEnvState env;current=&env;
 env.directory=fs::temp_directory_path()/"emucorev-cheat-bridge-probe";fs::create_directories(env.directory);
 const char* id="PCSE00333";auto path=env.directory/(std::string(id)+".psv");std::ofstream(path)<<"fixture";
 CheatModule module;module.name="main";module.segments[1]={0x4000,0x1000};cheat::add_module(env.cheat,module);
 check(cheat::load(env.cheat,env.directory,id),"initial pack loads");cheat::set_enabled(env.cheat,true,env.mem,{});
 cheat::apply(env.cheat,env.mem,{});check(read(env.mem,0x4010)==0x20000,"B200 segment1 writes before deletion");
 cheat::Cheat arm;arm.name="ARM";arm.enabled=true;arm.lines={{0xA200,0x5000,0xE1A00000}};
 {std::lock_guard lock(env.cheat.mutex);env.cheat.file.cheats.push_back(arm);}
 cheat::apply(env.cheat,env.mem,{});check(read(env.mem,0x5000)==0xE1A00000,"ARM patch installed");
 auto& operation=emucorev::savestate::save_state_operation_mutex();std::unique_lock hold(operation);
 std::promise<void> entered;auto ready=entered.get_future();
 auto deletion=std::async(std::launch::async,[&]{entered.set_value();return Java_com_sbro_emucorev_core_CheatBridge_deleteCheats(nullptr,0,id);});
 ready.wait();check(deletion.wait_for(std::chrono::milliseconds(50))==std::future_status::timeout,"JNI delete cannot access session while Load owns operation mutex");
 hold.unlock();check(deletion.get(),"live delete succeeds after operation owner releases");
 check(cheat::snapshot(env.cheat).cheats.empty(),"delete clears runtime pack");
 check(read(env.mem,0x5000)==0,"delete restores saved ARM original");
 check(read(env.mem,0x4010)==0x20000,"delete preserves normal data-write semantics");
 {std::lock_guard lock(env.cheat.mutex);check(env.cheat.modules.size()==1&&env.cheat.modules[0].segments[1].address==0x4000,"delete retains loaded executable segment bases");}
 std::ofstream(path)<<"fixture";check(cheat::reload(env.cheat,env.directory,id,env.mem,{}),"replacement pack imports without app restart");
 std::memset(env.mem.bytes.data()+0x4010,0,4);cheat::apply(env.cheat,env.mem,{});
 check(read(env.mem,0x4010)==0x20000,"B200 segment1 writes after delete/reimport");
 check(!cheat::snapshot(env.cheat).cheats[0].broken,"replacement relative cheat remains executable");
 cheat::unload(env.cheat);check(env.cheat.modules.empty(),"full session unload still clears module identity");
 fs::remove_all(env.directory);std::printf("%s actual cheat bridge/interpreter: %d checks, %d failures\n",failures?"FAIL":"PASS",checks,failures);return !!failures;
}
'''
(out/'probe.cpp').write_text(cpp)
if os.name=='nt':os.environ['PATH']='C:/msys64/ucrt64/bin;'+os.environ['PATH']
cxx=os.environ.get('CXX') or shutil.which('g++');assert cxx
subprocess.run([cxx,'-std=c++20','-pthread','-O0','-I'+str(support),'-I'+str(core/'cheat/include'),'-I'+str(root/'app/src/main/cpp/emucorev/include'),str(out/'probe.cpp'),'-o',str(out/'probe.exe')],check=True)
p=subprocess.run([str(out/'probe.exe')],capture_output=True,text=True,timeout=10)
print(p.stdout+p.stderr);(out/('red.log' if p.returncode else 'green.log')).write_text(p.stdout+p.stderr)
raise SystemExit(p.returncode)
