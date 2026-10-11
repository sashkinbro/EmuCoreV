#!/usr/bin/env python3
"""Exercise the actual depth-transfer cleanup tail with a Vulkan driver seam.

The image copy and submission precede this tail and are intentionally unchanged.
The seam counts fence/command-buffer lifetime, keeps the real request Queue, and
checks that a fence wait never borrows the command-pool mutex.
"""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
out = root / 'app/build/gpu-depth-cleanup-probe'
out.mkdir(parents=True, exist_ok=True)
renderer = root / 'app/src/main/cpp/vita3k/vita3k/renderer'
source = (renderer / 'src/vulkan/surface_cache.cpp').read_text()
start = source.index('bool VKSurfaceCache::try_transfer_depth_gpu(')
start = source.index('    dst_info->depth_content_stored = true;', start)
end = source.index('    LOG_INFO_ONCE("Depth transfer done on the GPU:', start)
tail = source[start:end]
# Replace the driver mutex type only; preserve lock scope and every decision.
tail = tail.replace('std::lock_guard<std::mutex>', 'std::lock_guard<PoolMutex>')
owner = renderer / 'include/renderer/vulkan/gpu_resource_cleanup.h'
owner_include = '#include <renderer/vulkan/gpu_resource_cleanup.h>\nusing namespace renderer::vulkan;\n' if owner.exists() else ''
reset_source = (renderer / 'include/renderer/vulkan/frame_runtime_reset.h').read_text()
def body(text, marker):
    begin = text.index(marker)
    opening = text.index('{', begin)
    depth = 0
    for end in range(opening, len(text)):
        depth += (text[end] == '{') - (text[end] == '}')
        if depth == 0:
            return text[begin:end + 1]
    raise AssertionError('Unterminated production body: ' + marker)
reset = body(reset_source, 'inline void reset_gpu_requests_after_join(')
cleanup_source = (renderer / 'src/vulkan/renderer.cpp').read_text()
cleanup_start = cleanup_source.index('void VKState::cleanup()')
cleanup_start = cleanup_source.index('    device.waitIdle();', cleanup_start)
cleanup_end = cleanup_source.index('    context = nullptr;', cleanup_start)
cleanup_prefix = cleanup_source[cleanup_start:cleanup_end]

cpp = r'''
#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <variant>
#include <threads/queue.h>
''' + owner_include + r'''
#define LOG_ERROR(...) ((void)0)
namespace vk { enum class Result {eSuccess}; constexpr bool True=true; }
using CallbackRequestFunction=std::function<void()>;
struct CallbackRequest {CallbackRequestFunction* callback; bool wait_for_gpu=false;};
using WaitThreadRequest=std::variant<CallbackRequest>;
int failures=0,checks=0;
void check(bool good,const char* what){++checks;if(!good){++failures;std::printf("FAIL %s\n",what);}}
struct PoolMutex {
 std::mutex mutex; std::atomic<bool> held=false;
 void lock(){mutex.lock();held=true;} void unlock(){held=false;mutex.unlock();}
};
struct State {
 PoolMutex multithread_pool_mutex; int multithread_command_pool=7;
 std::atomic<size_t> gpu_request_reader_count=0;
 Queue<WaitThreadRequest> request_queue;
 std::atomic<int> waits=0,destroys=0,frees=0;
 bool device_alive=true;
 struct Device {
  State& s;
  void waitIdle(){check(s.device_alive,"GPU idle precedes resource teardown");}
  vk::Result waitForFences(int,bool,uint64_t){
   check(s.device_alive,"fence wait precedes device destruction");
   check(!s.multithread_pool_mutex.held,"fence wait is outside pool mutex");++s.waits;return vk::Result::eSuccess;
  }
  void destroyFence(int){check(s.device_alive,"fence destruction precedes device destruction");++s.destroys;}
  void freeCommandBuffers(int,int){check(s.device_alive,"command free precedes pool destruction");check(s.multithread_pool_mutex.held,"command free holds pool mutex");++s.frees;}
 } device{*this};
 void normal_cleanup();
};
struct Info {bool depth_content_stored=false;};
struct VKSurfaceCache {
 State& state;
 void transfer_cleanup(Info* dst_info,int fence=1,int transfer_cmd=2){
''' + tail + r'''
 }
};
''' + reset + r'''
void State::normal_cleanup(){
''' + cleanup_prefix + r'''
 device_alive=false;
}
void consume(State& s){
 auto request=s.request_queue.pop();
 if(request){auto& cb=std::get<CallbackRequest>(*request);(*cb.callback)();delete cb.callback;}
}
void discard_after_join(State& s){
 reset_gpu_requests_after_join(s.request_queue);
}
void counts(State& s,const char* label){check(s.waits==1&&s.destroys==1&&s.frees==1,label);}
int main(){
 {
  State s;Info dst;VKSurfaceCache{s}.transfer_cleanup(&dst);
  check(dst.depth_content_stored,"no-reader cleanup preserves GPU depth-store result");
  counts(s,"no reader cleans submitted resources synchronously exactly once");
  check(s.request_queue.size()==0,"no reader leaves no stranded callback");discard_after_join(s);
 }
 {
  State s;s.gpu_request_reader_count=1;Info dst;VKSurfaceCache{s}.transfer_cleanup(&dst);
  check(s.waits==0&&s.request_queue.size()==1,"live reader retains asynchronous cleanup");
  consume(s);counts(s,"consumer plus callback deletion cleans exactly once");
 }
 {
  State s;s.gpu_request_reader_count=1;s.request_queue.abort();Info dst;VKSurfaceCache{s}.transfer_cleanup(&dst);
  counts(s,"publication rejection cleans submitted resources synchronously");
  check(s.request_queue.size()==0,"rejection leaves no callback payload");
 }
 {
  State s;s.gpu_request_reader_count=1;Info dst;VKSurfaceCache{s}.transfer_cleanup(&dst);
  s.request_queue.abort();s.gpu_request_reader_count=0;discard_after_join(s);
  counts(s,"queued callback disposal after join cleans resources exactly once");
 }
 {
  State s;s.gpu_request_reader_count=1;Info dst;
  std::thread reader([&]{consume(s);});VKSurfaceCache{s}.transfer_cleanup(&dst);reader.join();
  counts(s,"immediate concurrent consumption preserves exactly-once ownership");
 }
 {
  State s;s.gpu_request_reader_count=1;Info dst;VKSurfaceCache{s}.transfer_cleanup(&dst);
  s.gpu_request_reader_count=0;s.normal_cleanup();
  counts(s,"normal cleanup disposes queued resource owner before driver teardown");
  check(s.request_queue.is_aborted()&&s.request_queue.size()==0,"cleanup keeps request admission closed");
 }
'''
if owner.exists():
    cpp += r'''
 {
  State s;
  check(s.gpu_request_reader_count==0,"no request reader before entry");
  try{ActiveGpuRequestReader reader(s.gpu_request_reader_count);check(s.gpu_request_reader_count==1,"actual reader entry is published");throw std::runtime_error("exit");}
  catch(const std::runtime_error&){}
  check(s.gpu_request_reader_count==0,"reader exit releases actual count even on unwind");
 }
 {
  int attempts=0,reports=0;
  {GpuResourceCleanup owner([&]{++attempts;throw std::runtime_error("driver lost");},[&]{++reports;});}
  check(attempts==1&&reports==1,"disposal reports driver error and does not retry invalid resources");
 }
'''
cpp += r'''
 std::printf("GPU depth cleanup: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
'''
(out / 'probe.cpp').write_text(cpp)
if os.name == 'nt' and Path('C:/msys64/ucrt64/bin').is_dir():
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
compiler = os.environ.get('CXX') or shutil.which('g++') or shutil.which('clang++')
assert compiler, 'A C++20 compiler is required'
exe = out / 'probe.exe'
subprocess.run([compiler, '-std=c++20', '-pthread', '-I' + str(renderer / 'include'),
                '-I' + str(root / 'app/src/main/cpp/vita3k/vita3k/threads/include'),
                str(out / 'probe.cpp'), '-o', str(exe)], check=True)
result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
print(result.stdout + result.stderr)
(out / ('red.log' if result.returncode else 'green.log')).write_text(result.stdout + result.stderr)
raise SystemExit(result.returncode)
