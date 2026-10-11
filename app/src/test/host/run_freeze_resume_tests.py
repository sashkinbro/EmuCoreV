#!/usr/bin/env python3
"""Run exact resume/freeze bodies with a controlled CV reacquisition boundary.

Only the CV transport is replaced. The parked waiter cannot regain its thread
mutex until the controller has resumed, re-paused and requested world capture.
An optional source path permits a preserved baseline RED run.
"""
from pathlib import Path
import os, shutil, subprocess, sys

root = Path(__file__).resolve().parents[4]
out = root / 'app/build/freeze-resume-probe'
out.mkdir(parents=True, exist_ok=True)
source = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'app/src/main/cpp/vita3k/vita3k/kernel/src/thread_wait.cpp'
text = source.read_text(encoding='utf-8-sig')

def body(marker):
    start = text.index(marker)
    brace = text.index('{', start)
    depth = 0
    for end in range(brace, len(text)):
        depth += (text[end] == '{') - (text[end] == '}')
        if depth == 0:
            return text[start:end + 1]
    raise AssertionError(marker)

methods = ['bool ThreadState::wait_for_guest_resume(', 'void ThreadState::resume_after_session_pause(',
           'void ThreadState::resume_if_suspended(', 'bool ThreadState::resume_from_world(',
           'void ThreadState::resume(bool', 'ThreadStatus ThreadState::pause_for_session(',
           'void ThreadState::request_world_stop(', 'bool ThreadState::wait_world_stopped(',
           'void ThreadState::park_creation_for_capture(', 'void ThreadState::resume_creation_after_capture(']
cpp = r'''
#include <mutex>
#include <condition_variable>
#include <future>
#include <chrono>
#include <thread>
#include <cassert>
#include <cstdio>
#include <memory>
#include <vector>
#include <array>
#include <cstdint>
#include <algorithm>
using namespace std::chrono_literals;
#define LOG_WARN(...) ((void)0)
struct CPU{};void stop(CPU&){}void guest_sched_release_for_block(){}
enum class ThreadStatus{running,waiting,dormant,suspended};
enum class WaitOperation{creation_import};
struct WaitContinuation{struct{uint64_t frame_sequence;WaitOperation operation;std::array<uint32_t,8>args;int context;}state;};
struct LatchingCV {
 std::condition_variable cv;std::promise<void> enrolled,reacquired,allow_reacquire;
 std::shared_future<void> allowed=allow_reacquire.get_future().share();bool first=true,latched=false;
 template<class P>void wait(std::unique_lock<std::mutex>&l,P pred){while(!pred()){
   if(first){first=false;enrolled.set_value();}cv.wait(l);
   if(!latched){latched=true;l.unlock();reacquired.set_value();allowed.wait();l.lock();}
 }}
 template<class P>bool wait_until(std::unique_lock<std::mutex>&l,std::chrono::steady_clock::time_point d,P p){return cv.wait_until(l,d,p);}
 void notify_all(){cv.notify_all();}
};
struct ThreadState {
 std::mutex mutex;LatchingCV status_cond;std::condition_variable wait_cv;
 std::unique_ptr<CPU>cpu=std::make_unique<CPU>();ThreadStatus status=ThreadStatus::running;
 bool world_stop_requested=false,vm_suspended=false,debugger_suspended=false,session_suspended=false;
 bool freeze_waiting=false,world_stopped=false,delete_requested=false,single_stepping=false,suspend_requested=false;
 const char*name="host fixture";int id=1;
 std::shared_ptr<WaitContinuation>creation_wait;std::vector<std::shared_ptr<WaitContinuation>>wait_continuations;
 uint64_t next_continuation_sequence=1;uint32_t last_import_nid=0;int last_import_context=0;
 bool exiting(){return delete_requested;}void update_status(ThreadStatus s){status=s;status_cond.notify_all();}
 bool wait_for_guest_resume(std::unique_lock<std::mutex>&);void resume_after_session_pause();
 void resume_if_suspended();bool resume_from_world();void resume(bool);ThreadStatus pause_for_session();
 void request_world_stop();bool wait_world_stopped(std::chrono::steady_clock::time_point);
 void park_creation_for_capture();void resume_creation_after_capture();
};
''' + '\n'.join(body(m) for m in methods) + r'''
int main(){unsigned failures=0;auto check=[&](bool b,const char*s){printf("%s %s\n",b?"PASS":"FAIL",s);failures+=!b;};
 for(int mode=0;mode<4;++mode){ThreadState t;
  if(mode==0)t.session_suspended=true;if(mode==1)t.vm_suspended=true;
  if(mode==2)t.world_stop_requested=true;if(mode==3)t.debugger_suspended=true;
  auto enrolled=t.status_cond.enrolled.get_future(),reacquired=t.status_cond.reacquired.get_future();
  auto worker=std::async(std::launch::async,[&]{std::unique_lock l(t.mutex);return t.wait_for_guest_resume(l);});
  enrolled.wait();
  if(mode==0)t.resume_after_session_pause();if(mode==1)t.resume_if_suspended();
  if(mode==2)t.resume_from_world();if(mode==3)t.resume(false);
  reacquired.wait();
  {std::lock_guard l(t.mutex);check(t.freeze_waiting,"actual freeze gate remains host-parked");
   check(t.status==ThreadStatus::suspended,"external resume does not prematurely publish RUNNING");}
  t.pause_for_session();t.request_world_stop();
  check(t.wait_world_stopped(std::chrono::steady_clock::now()+25ms),"re-pause plus capture accepts host freeze waiter as quiescent");
  {std::lock_guard l(t.mutex);t.delete_requested=true;t.status_cond.notify_all();}
  t.status_cond.allow_reacquire.set_value();
  check(worker.wait_for(1s)==std::future_status::ready,"deletion unwinds parked host within bound");
  check(!worker.get(),"deleted freeze gate does not reenter guest");
 }
 ThreadState creator;creator.park_creation_for_capture();creator.resume_from_world();creator.resume_creation_after_capture();
 check(creator.status==ThreadStatus::running,"creation admission owner promotes its parked import after capture");
 check(creator.wait_continuations.empty(),"creation admission retires its durable completion exactly once");
 printf("%u failures\n",failures);return failures?1:0;}
'''
(out / 'probe.cpp').write_text(cpp)
if os.name == 'nt':
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler, 'A C++20 compiler is required'
subprocess.run([compiler, '-std=c++20', '-pthread', str(out / 'probe.cpp'), '-o', str(out / 'probe.exe')], check=True)
result = subprocess.run([str(out / 'probe.exe')], capture_output=True, text=True, timeout=10)
print(result.stdout + result.stderr)
(out / ('red.log' if result.returncode else 'green.log')).write_text(result.stdout + result.stderr)
raise SystemExit(result.returncode)
