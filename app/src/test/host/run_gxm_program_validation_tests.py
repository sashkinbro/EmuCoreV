#!/usr/bin/env python3
"""Run renderer program validation with real reference-count boundary values."""
from pathlib import Path
import os
import subprocess

root = Path(__file__).resolve().parents[4]
source = (root/'app/src/main/cpp/vita3k/vita3k/renderer/src/state_set.cpp').read_text()
out = root/'app/build/gxm-program-validation-probe'
out.mkdir(parents=True,exist_ok=True)
def function(marker):
    start=source.index(marker); opening=source.index('{',start); depth=0
    for end in range(opening,len(source)):
        depth+=(source[end]=='{')-(source[end]=='}')
        if not depth:return source[start:end+1]
    raise AssertionError(marker)
cpp=r'''
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
struct MemState {};
struct SceGxmProgram {uint32_t magic=0x00505847;bool vertex=false;bool is_vertex()const{return vertex;}};
struct GuestProgram {const SceGxmProgram* pointer;const SceGxmProgram* get(const MemState&)const{return pointer;}};
struct SceGxmFragmentProgram {bool renderer_data=true;std::atomic<uint32_t> reference_count{1};GuestProgram program;};
struct SceGxmVertexProgram {bool renderer_data=true;std::atomic<uint32_t> reference_count{1};GuestProgram program;};
'''+function('static bool is_sane_fragment_program(')+'\n'+function('static bool is_sane_vertex_program(')+r'''
int main(){
 unsigned checks=0,failures=0;
 auto check=[&](bool good,const char*label){++checks;if(!good){++failures;printf("FAIL %s\n",label);}};
 MemState mem;SceGxmProgram fragment{},vertex{0x00505847,true};
 SceGxmFragmentProgram fp{true,1,{&fragment}};SceGxmVertexProgram vp{true,1,{&vertex}};
 for(uint32_t refs:{1u,99999u,100000u,100001u,198488u,1000000u,UINT32_MAX}){
  fp.reference_count=refs;vp.reference_count=refs;
  check(is_sane_fragment_program(mem,&fp),"live fragment program accepts a positive uint32 reference count");
  check(is_sane_vertex_program(mem,&vp),"live vertex program accepts a positive uint32 reference count");
 }
 fp.reference_count=0;vp.reference_count=0;
 check(!is_sane_fragment_program(mem,&fp),"released fragment rejected");check(!is_sane_vertex_program(mem,&vp),"released vertex rejected");
 fp.reference_count=1;vp.reference_count=1;fp.renderer_data=false;vp.renderer_data=false;
 check(!is_sane_fragment_program(mem,&fp),"missing fragment renderer object rejected");check(!is_sane_vertex_program(mem,&vp),"missing vertex renderer object rejected");
 fp.renderer_data=true;vp.renderer_data=true;fragment.magic=0;vertex.magic=0;
 check(!is_sane_fragment_program(mem,&fp),"invalid fragment magic rejected");check(!is_sane_vertex_program(mem,&vp),"invalid vertex magic rejected");
 vertex.magic=0x00505847;vertex.vertex=false;
 check(!is_sane_vertex_program(mem,&vp),"fragment binary cannot be a vertex shader");
 check(!is_sane_fragment_program(mem,nullptr),"null fragment rejected");check(!is_sane_vertex_program(mem,nullptr),"null vertex rejected");
 printf("GXM program validation: %u checks, %u failures\n",checks,failures);return !!failures;
}
'''
(out/'probe.cpp').write_text(cpp)
compiler=Path('C:/msys64/ucrt64/bin/g++.exe');env=os.environ.copy();env['PATH']=str(compiler.parent)+';'+env.get('PATH','')
subprocess.run([str(compiler),'-std=c++20','-O0',str(out/'probe.cpp'),'-o',str(out/'probe.exe')],check=True,env=env)
run=subprocess.run([str(out/'probe.exe')],capture_output=True,text=True,env=env,timeout=10)
print(run.stdout+run.stderr)
(out/('green.log' if not run.returncode else 'red.log')).write_text(run.stdout+run.stderr)
raise SystemExit(run.returncode)
