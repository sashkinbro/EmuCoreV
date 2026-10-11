#!/usr/bin/env python3
# Probe actual Vulkan constructor/idle bodies and callback publication helper.
from pathlib import Path
import os,re,shutil,subprocess
root=Path(__file__).resolve().parents[4]
base=root/'app/src/main/cpp/vita3k/vita3k'
out=root/'app/build/cold-gpu-idle-probe';out.mkdir(parents=True,exist_ok=True)
source=(base/'renderer/src/vulkan/renderer.cpp').read_text()
header=(base/'renderer/include/renderer/state.h').read_text()
ctor=source[source.index('VKState::VKState(int gpu_idx)'):source.index('bool VKState::init()')]
idle=source[source.index('void VKState::wait_gpu_idle()'):source.index('void VKState::unmap_memory')]
field=re.search(r'    Context \*context[^\n]*',header).group(0)
helper_source=(base/'renderer/include/renderer/vulkan/frame_runtime_reset.h').read_text()
helper=helper_source[helper_source.index('inline bool enqueue_gpu_request_callback'):helper_source.index('// Only after all old queue readers')]
cpp=r'''
#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <functional>
#include <cstdio>
#include <new>
#include <thread>
#include <variant>
#include <threads/queue.h>
struct Context{};struct State { FIELD virtual ~State()=default; };
struct Component {template<class T> Component(T&) {}};
using CallbackRequestFunction=std::function<void()>;
struct CallbackRequest {CallbackRequestFunction* callback;bool wait_for_gpu;};
using WaitThreadRequest=std::variant<CallbackRequest>;
HELPER
struct Device {std::atomic<int> idle_calls{0};void waitIdle(){++idle_calls;}};
struct VKState:State {int gpu_idx;Component surface_cache,pipeline_cache,texture_cache,screen_renderer,buffer_trapping;Device device;struct{bool enable_memory_mapping=true;}features;Queue<WaitThreadRequest> request_queue;std::atomic<size_t> gpu_request_reader_count{0};VKState(int);void wait_gpu_idle();};
CTOR
IDLE
int checks=0,failures=0;
void check(bool value,const char*msg){++checks;if(!value){++failures;std::printf("FAIL %s\n",msg);}}
void discard(VKState&s){for(auto&r:s.request_queue.snapshot_items())delete std::get<CallbackRequest>(r).callback;s.request_queue.reset();}
int main(){
 {alignas(VKState) unsigned char storage[sizeof(VKState)];std::memset(storage,0xA5,sizeof(storage));auto*s=new(storage)VKState(0);unsigned char bytes[sizeof(Context*)];std::memcpy(bytes,&s->context,sizeof(bytes));bool allzero=true;for(auto v:bytes)allzero&=v==0;check(allzero,"cold Vulkan constructor initializes inherited context");s->~VKState();}
 {VKState s(0);Context context;s.context=&context;std::atomic<bool>done=false;std::thread load([&]{s.wait_gpu_idle();done=true;});std::this_thread::sleep_for(std::chrono::milliseconds(80));check(done,"zero readers does not wait for an impossible callback");check(s.request_queue.size()==0,"zero readers does not publish marker");check(s.device.idle_calls==1,"zero readers still waits for actual device idle");s.request_queue.abort();load.join();discard(s);}
 {VKState s(0);Context context;s.context=&context;s.gpu_request_reader_count=1;std::atomic<bool>done=false;std::thread load([&]{s.wait_gpu_idle();done=true;});auto marker=s.request_queue.pop(50000);check(bool(marker),"active reader path publishes completion marker");check(!done,"active reader drain waits until callback executes");if(marker){auto cb=std::get<CallbackRequest>(*marker);(*cb.callback)();delete cb.callback;}load.join();check(done&&s.device.idle_calls==1,"active reader finishes after GPU idle and callback");}
 {VKState s(0);Context context;s.context=&context;s.gpu_request_reader_count=1;s.request_queue.abort();s.wait_gpu_idle();check(s.request_queue.size()==0&&s.device.idle_calls==1,"aborted queue allocates no retained marker and still idles device");}
 {VKState s(0);s.context=nullptr;s.features.enable_memory_mapping=false;s.wait_gpu_idle();check(s.request_queue.size()==0&&s.device.idle_calls==1,"mapping disabled retains real device idle");}
 {Queue<WaitThreadRequest>q;q.abort();auto owner=std::make_shared<int>(1);std::weak_ptr<int>weak=owner;auto cb=std::make_unique<CallbackRequestFunction>([owner]{});owner.reset();const bool accepted=enqueue_gpu_request_callback(q,std::move(cb),false);check(!accepted&&weak.expired(),"rejected marker releases callback ownership");}
 std::printf("Cold GPU idle: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
'''.replace('FIELD',field).replace('CTOR',ctor).replace('IDLE',idle).replace('HELPER',helper)
(out/'probe.cpp').write_text(cpp)
if os.name=='nt' and Path('C:/msys64/ucrt64/bin').is_dir():os.environ['PATH']='C:/msys64/ucrt64/bin;'+os.environ['PATH']
compiler=os.environ.get('CXX') or shutil.which('g++') or shutil.which('clang++');assert compiler
exe=out/'probe.exe';subprocess.run([compiler,'-std=c++20','-pthread','-I'+str(base/'threads/include'),str(out/'probe.cpp'),'-o',str(exe)],check=True)
p=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10);print(p.stdout+p.stderr);(out/('red.log' if p.returncode else 'green.log')).write_text(p.stdout+p.stderr);raise SystemExit(p.returncode)
