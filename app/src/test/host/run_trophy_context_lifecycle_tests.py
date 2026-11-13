#!/usr/bin/env python3
"""Read-only regression probe using real Context/NpState and actual export bodies.

Guest export ABI/tracing and file close are adapted; no disk/game/device state
is modified. Each case isolates invalid-index aborts from the other scenarios.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess

root=next(p for p in Path(__file__).resolve().parents if (p/'app/src/main/cpp/vita3k/vita3k').is_dir())
base=root/'app/src/main/cpp/vita3k/vita3k'
out=root/'tools/system-services/trophy-context-host'
out.mkdir(parents=True,exist_ok=True)
source=(base/'np/src/trophy/context.cpp').read_text(encoding='utf-8')
helpers=source[source.index('np::trophy::Context *get_trophy_context'):]
module=(base/'modules/SceNpTrophy/SceNpTrophy.cpp').read_text(encoding='utf-8')
errors=module[module.index('enum SceNpTrophyErrorCode'):module.index('#define SCE_NP_TROPHY_GAME_TITLE_MAX_SIZE')]
destroy=module[module.index('EXPORT(int, sceNpTrophyDestroyContext'):module.index('EXPORT(int, sceNpTrophyDestroyHandle')]
query=module[module.index('EXPORT(int, sceNpTrophyGetTrophyUnlockState'):module.index('EXPORT(int, sceNpTrophyInit')]
cpp=r'''
#include <np/state.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
struct IOState{};
std::vector<int> closed;
int close_file(IOState&,int fd,const char*){closed.push_back(fd);return 0;}
struct EmuEnvState{NpState np;};
using SceNpTrophyHandle=int32_t;
#define EXPORT(ret,name,...) ret name(EmuEnvState&emuenv,int thread_id,const char*export_name,##__VA_ARGS__)
#define TRACY_FUNC(...)
'''+errors+helpers+destroy+query+r'''
int main(int argc,char**argv){if(argc!=2)return 2;std::string test=argv[1];EmuEnvState env;IOState io;int fails=0;
 auto check=[&](bool ok,const char*message){printf("%s %s\n",ok?"PASS":"FAIL",message);fails+=!ok;};
 auto live=[&]{auto&ctx=env.np.trophy_state.contexts.emplace_back();ctx.valid=true;ctx.id=1;ctx.io=&io;ctx.trophy_file_stream=42;ctx.trophy_count=3;std::fill_n(ctx.trophy_progress,4,0);ctx.trophy_progress[0]=5;env.np.trophy_state.inited=true;};
 auto&state=env.np.trophy_state;
 if(test=="get_zero_empty"){check(get_trophy_context(state,0)==nullptr,"zero lookup on empty table returns no context");}
 else if(test=="get_zero_live"){live();check(get_trophy_context(state,0)==nullptr,"zero lookup on live table returns no context");}
 else if(test=="destroy_zero_empty"){check(!destroy_trophy_context(state,0)&&closed.empty(),"zero destroy on empty table is rejected without IO");}
 else if(test=="destroy_zero_live"){live();check(!destroy_trophy_context(state,0)&&closed.empty()&&state.contexts[0].valid,"zero destroy cannot close or mutate another live context");}
 else if(test=="get_negative"){live();check(get_trophy_context(state,-1)==nullptr&&get_trophy_context(state,INT32_MIN)==nullptr,"negative handles rejected");}
 else if(test=="get_outside"){live();check(get_trophy_context(state,2)==nullptr&&get_trophy_context(state,INT32_MAX)==nullptr,"out-of-range handles rejected");}
 else if(test=="get_dead"){live();state.contexts[0].valid=false;check(get_trophy_context(state,1)==nullptr,"destroyed context cannot be retrieved");}
 else if(test=="destroy_then_get"){live();check(destroy_trophy_context(state,1),"valid context destroyed");check(get_trophy_context(state,1)==nullptr,"lookup after destroy is invalid");check(closed==std::vector<int>{42},"underlying trophy descriptor closed exactly once");}
 else if(test=="repeat_destroy"){live();check(destroy_trophy_context(state,1)&&!destroy_trophy_context(state,1)&&closed.size()==1,"repeat destroy is rejected without duplicate close");}
 else if(test=="live_query"){live();np::trophy::TrophyFlagArray flags{};uint32_t count=0;check(sceNpTrophyGetTrophyUnlockState(env,0,"test",1,1,&flags,&count)==0&&count==3&&flags[0]==5,"actual exported query still returns live local progress");}
 else if(test=="dead_query"){live();destroy_trophy_context(state,1);np::trophy::TrophyFlagArray flags;std::fill_n(flags,4,0xCCCCCCCC);uint32_t count=0xEEEEEEEE;
  check(uint32_t(sceNpTrophyGetTrophyUnlockState(env,0,"test",1,1,&flags,&count))==SCE_NP_TROPHY_ERROR_INVALID_CONTEXT,"actual query rejects destroyed context using existing error");
  check(count==0xEEEEEEEE&&std::all_of(std::begin(flags),std::end(flags),[](uint32_t n){return n==0xCCCCCCCC;}),"failed query preserves guest outputs");}
 else if(test=="zero_query"){live();np::trophy::TrophyFlagArray flags{};uint32_t count=0;check(uint32_t(sceNpTrophyGetTrophyUnlockState(env,0,"test",0,1,&flags,&count))==SCE_NP_TROPHY_ERROR_INVALID_CONTEXT,"actual query rejects zero handle using existing error");}
 else if(test=="zero_export_destroy"){live();check(uint32_t(sceNpTrophyDestroyContext(env,0,"test",0))==SCE_NP_TROPHY_ERROR_INVALID_CONTEXT&&closed.empty(),"actual destroy export rejects zero without closing live trophy IO");}
 else if(test=="dead_export_destroy"){live();state.contexts[0].valid=false;check(uint32_t(sceNpTrophyDestroyContext(env,0,"test",1))==SCE_NP_TROPHY_ERROR_INVALID_CONTEXT&&closed.empty(),"actual destroy export rejects dead context");}
 else return 2;
 return fails?1:0;
}
'''
(out/'probe.cpp').write_text(cpp,encoding='utf-8')
compiler=os.environ.get('CXX')or shutil.which('g++')or'C:/msys64/ucrt64/bin/g++.exe'
env=os.environ.copy();env['PATH']=str(Path(compiler).parent)+os.pathsep+env['PATH']
includes=[base/'np/include',base/'mem/include',base/'util/include',root/'app/src/main/cpp/vita3k/external/boost',root/'app/src/main/cpp/vita3k/external/fmt/include']
subprocess.run([compiler,'-std=c++20','-O0','-D_GLIBCXX_DEBUG',*['-I'+str(i)for i in includes],str(out/'probe.cpp'),'-o',str(out/'probe.exe')],env=env,check=True)
cases=['get_zero_empty','get_zero_live','destroy_zero_empty','destroy_zero_live','get_negative','get_outside','get_dead','destroy_then_get','repeat_destroy','live_query','dead_query','zero_query','zero_export_destroy','dead_export_destroy']
failed=0;results=[]
for case in cases:
    result=subprocess.run([str(out/'probe.exe'),case],env=env,capture_output=True,text=True,timeout=8)
    ok=result.returncode==0;failed+=not ok
    detail=f"{'PASS'if ok else'FAIL'} CASE {case} exit={result.returncode}\n"+result.stdout+result.stderr
    results.append(detail);print(detail,end='')
summary=f'{len(cases)-failed}/{len(cases)} cases passed\n';print(summary,end='')
(out/'result.log').write_text('\n'.join(results)+summary,encoding='utf-8')
raise SystemExit(bool(failed))
