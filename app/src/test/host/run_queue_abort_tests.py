#!/usr/bin/env python3
"""Expose the real Queue predicate/abort race with scheduling hooks only.

The production pop, push and abort methods are unchanged apart from pauses
immediately before wait enrollment and notification observation. A missed wake
is cleaned up by a second abort before joining, so the regression stays bounded.
"""
from pathlib import Path
import subprocess,os,shutil
root=Path(__file__).resolve().parents[4];out=root/'app/build/queue-abort-race-probe';out.mkdir(exist_ok=True)
s=(root/'app/src/main/cpp/vita3k/vita3k/threads/include/threads/queue.h').read_text(encoding='utf-8')
a=s.index('    std::unique_ptr<T> pop(');b=s.index('    void push(',a);part=s[a:b];assert part.count('condempty_.wait(mlock);')==1
part=part.replace('condempty_.wait(mlock);','before_pop_wait();\n                    condempty_.wait(mlock);');s=s[:a]+part+s[b:]
a=s.index('    void push(');b=s.index('    // Guest waits',a);part=s[a:b];assert part.count('cond_.wait(mlock);')==1
part=part.replace('cond_.wait(mlock);','before_pop_wait();\n                cond_.wait(mlock);');s=s[:a]+part+s[b:]
a=s.index('    void abort()');b=s.index('    bool is_aborted()',a);part=s[a:b];assert part.count('        notify_changes();')==1
part=part.replace('        notify_changes();','        after_abort_notify();\n        notify_changes();');s=s[:a]+part+s[b:]
(out/'queue_probe.h').write_text(s,encoding='utf-8')
cpp=r'''
#include <future>
#include <thread>
#include <cstdio>
#include <atomic>
#include <string>
std::promise<void> prewait_entered,allow_wait,abort_notified;
std::shared_future<void> wait_permission=allow_wait.get_future().share();
void before_pop_wait(){prewait_entered.set_value();wait_permission.wait();}
void after_abort_notify(){static std::atomic<bool> first{true};if(first.exchange(false))abort_notified.set_value();}
#include "queue_probe.h"
int main(int argc,char **argv){
 const bool producer=argc>1&&std::string(argv[1])=="push";
 Queue<int> queue;if(producer){queue.maxPendingCount_=1;queue.push(1);}
 auto entered=prewait_entered.get_future(),notified=abort_notified.get_future();
 std::promise<bool> done;auto complete=done.get_future();
 std::thread reader([&]{if(producer){queue.push(2);done.set_value(queue.size()==1);}else done.set_value(!queue.pop());});
 entered.wait();
 std::thread aborter([&]{queue.abort();});
 const bool notified_before_wait=notified.wait_for(std::chrono::milliseconds(30))==std::future_status::ready;
 allow_wait.set_value();
 const bool completed=complete.wait_for(std::chrono::milliseconds(100))==std::future_status::ready;
 queue.abort();
 reader.join();aborter.join();
 std::printf("abort notified before wait enrollment: %s\n",notified_before_wait?"YES":"NO");
 std::printf("%s production Queue::abort wakes reader/producer at predicate/wait boundary\n",completed?"PASS":"FAIL");
 return completed?0:1;
}
'''
(out/'probe.cpp').write_text(cpp)
if os.name == 'nt' and Path('C:/msys64/ucrt64/bin').is_dir():
 os.environ['PATH']='C:/msys64/ucrt64/bin;'+os.environ['PATH']
compiler=os.environ.get('CXX') or shutil.which('g++');assert compiler, 'A C++20 compiler is required'
subprocess.run([str(compiler),'-std=c++20','-O0','-pthread',str(out/'probe.cpp'),'-o',str(out/'probe.exe')],check=True)
failed=0;logs=[]
for mode in ('pop','push'):
 r=subprocess.run([str(out/'probe.exe'),mode],capture_output=True,text=True,timeout=5)
 print(mode+':\n'+r.stdout+r.stderr);logs.append(mode+':\n'+r.stdout+r.stderr);failed+=bool(r.returncode)
(out/('red.log' if failed else 'green.log')).write_text('\n'.join(logs));raise SystemExit(bool(failed))
