#!/usr/bin/env python3
"""Exercise the production readiness state without Vulkan or Android."""
from pathlib import Path
import os, shutil, subprocess
root = Path(__file__).resolve().parents[4]
out = root / 'app/build/first-present-probe'; out.mkdir(parents=True, exist_ok=True)
cpp = r'''
#include <renderer/first_present.h>
#include <cstdio>
#include <vector>
#include <utility>
int main() {
    renderer::FirstPresentReadiness ready;
    std::vector<std::pair<uint64_t,bool>> events;
    auto callback=[&](uint64_t generation,bool complete){events.emplace_back(generation,complete);};
    int failures=0;
    auto check=[&](bool condition,const char*label){if(!condition){++failures;std::printf("FAIL %s\n",label);}};
    ready.fresh_frame();
    auto before_arm=ready.begin_frame(true);
    const auto first=ready.arm(callback);
    check(events.size()==1&&events.back()==std::pair(first,false),"arm announces pending generation");
    ready.presented(before_arm,true);
    check(events.size()==1,"pre-arm rendering cannot complete restore");
    ready.fresh_frame();
    ready.presented(ready.begin_frame(false),true);
    check(events.size()==1,"empty or overlay-only frame cannot complete");
    auto frame=ready.begin_frame(true);
    ready.presented(frame,false);
    check(events.size()==1,"failed or skipped swap cannot complete");
    ready.presented(frame,true);
    check(events.size()==2&&events.back()==std::pair(first,true),"fresh successful presentation completes");
    ready.presented(frame,true);
    check(events.size()==2,"completion fires exactly once");
    const auto second=ready.arm(callback);
    ready.fresh_frame(); auto stale=ready.begin_frame(true);
    const auto third=ready.arm(callback);
    ready.presented(stale,true);
    check(events.size()==4,"older generation cannot complete a newer load");
    ready.fresh_frame(); auto cancelled=ready.begin_frame(true); ready.cancel();
    ready.presented(cancelled,true);
    check(events.size()==4,"stop cancels outstanding completion");
    check(second>first&&third>second,"generations increase across repeated load");
    renderer::FirstPresentReadiness recreated;
    const auto fourth=recreated.arm([&](uint64_t generation,bool complete){
        // Callbacks may query readiness without being invoked under its mutex.
        recreated.begin_frame(false);
        events.emplace_back(generation,complete);
    });
    check(fourth>third,"renderer recreation cannot reuse a stale JNI token");
    // No guest flip: a restored menu/game waiting for user input must still
    // release its input mask after presenting the actual restored image.
    recreated.presented(recreated.begin_frame(true),true);
    check(events.back()==std::pair(fourth,true),"static restored framebuffer completes without another guest flip");
    std::printf("%s first-present readiness (%d failures)\n",failures?"FAIL":"PASS",failures);
    return failures!=0;
}
'''
(out/'probe.cpp').write_text(cpp,encoding='utf-8')
if os.name=='nt': os.environ['PATH']='C:/msys64/ucrt64/bin;'+os.environ['PATH']
compiler=os.environ.get('CXX') or shutil.which('g++'); assert compiler
subprocess.run([compiler,'-std=c++20','-pthread','-I'+str(root/'app/src/main/cpp/vita3k/vita3k/renderer/include'),str(out/'probe.cpp'),'-o',str(out/'probe.exe')],check=True)
r=subprocess.run([str(out/'probe.exe')],capture_output=True,text=True,timeout=5)
print(r.stdout+r.stderr);(out/('red.log' if r.returncode else 'green.log')).write_text(r.stdout+r.stderr)
raise SystemExit(r.returncode)
