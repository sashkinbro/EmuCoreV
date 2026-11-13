#!/usr/bin/env python3
"""Run production rebind checks and pipeline input selection with poisoned guest GXP.

Compiles host adapters only (no Android build). Production GXP layouts, accessor,
binding predicates and pipeline source-selection expressions are used verbatim.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess

root = next(p for p in Path(__file__).resolve().parents if (p / 'app/src/main/cpp/vita3k/vita3k').is_dir())
native = root / 'app/src/main/cpp/vita3k/vita3k'
out = root / 'tools/retro-crash/gxp-continuation-host'
out.mkdir(parents=True, exist_ok=True)
state = (native / 'renderer/src/state_set.cpp').read_text(encoding='utf-8')
predicates = state[state.index('static bool is_sane_fragment_program'):state.index('COMMAND_SET_STATE(program)')]
module = (native / 'modules/SceGxm/SceGxm.cpp').read_text(encoding='utf-8')
releases = module[module.index('EXPORT(int, sceGxmShaderPatcherReleaseFragmentProgram'):module.index('EXPORT(int, sceGxmShaderPatcherSetAuxiliarySurface')]
pipeline = (native / 'renderer/src/vulkan/pipeline_cache.cpp').read_text(encoding='utf-8')
pipeline = pipeline[pipeline.index('vk::Pipeline PipelineCache::compile_pipeline'):]
fragment_expression = re.search(r'const SceGxmProgram \*gxm_fragment_shader = ([^;]+);', pipeline).group(1)
vertex_expression = re.search(r'retrieve_shader\(([^,]+), vertex_program.hash', pipeline).group(1)
header = native / 'renderer/include/renderer/program_source.h'
accessor = re.sub(r'^#(?:include[^\n]*|pragma once)', '', header.read_text(encoding='utf-8'), flags=re.M) if header.exists() else ''
cpp = r'''
#include <gxm/types.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <map>
#include <thread>
#include <vector>
struct MemState {alignas(16) uint8_t bytes[4096]{};};
template<class T>struct Ptr{uint32_t addr=0;uint32_t address()const{return addr;}explicit operator bool()const{return addr!=0;}bool operator==(const Ptr&)const=default;T*get(const MemState&m)const{return addr?reinterpret_cast<T*>(const_cast<uint8_t*>(m.bytes)+addr):nullptr;}};
namespace renderer {struct ShaderProgram{std::shared_ptr<const std::vector<uint8_t>> program_source;};}
struct SceGxmFragmentProgram{std::atomic<uint32_t>reference_count=1;Ptr<const SceGxmProgram>program;std::unique_ptr<renderer::ShaderProgram>renderer_data;std::atomic<int>compile_threads_on=0;};
struct SceGxmVertexProgram{std::atomic<uint32_t>reference_count=1;Ptr<const SceGxmProgram>program;std::unique_ptr<renderer::ShaderProgram>renderer_data;std::atomic<int>compile_threads_on=0;};
using FragmentProgramCache=std::map<int,Ptr<SceGxmFragmentProgram>>;using VertexProgramCache=std::map<int,Ptr<SceGxmVertexProgram>>;
struct SceGxmShaderPatcher{FragmentProgramCache fragment_program_cache;VertexProgramCache vertex_program_cache;};
struct EmuEnvState{MemState&mem;struct{std::map<uint32_t,std::shared_ptr<const std::vector<uint8_t>>>fragment_programs,vertex_programs;}gxm;std::unique_ptr<int>renderer=std::make_unique<int>(0);};
namespace renderer{void finish(int&,void*){}}
void guest_sched_release_for_block(){}
template<class T>void free_callbacked(EmuEnvState&,int,SceGxmShaderPatcher*,Ptr<T>){} // guest allocator deliberately does not call a C++ destructor
#define EXPORT(ret,name,...) ret name(EmuEnvState&emuenv,int thread_id,const char*export_name,##__VA_ARGS__)
#define TRACY_FUNC(...)
#define RET_ERROR(x) x
'''+accessor+predicates+releases+r'''
int main(){int failed=0;auto check=[&](bool ok,const char*name){printf("%s %s\n",ok?"PASS":"FAIL",name);failed+=!ok;};
 MemState mem;SceGxmFragmentProgram fragment_program_gxm;SceGxmVertexProgram vertex_program_gxm;
 fragment_program_gxm.program.addr=128;vertex_program_gxm.program.addr=1024;
 fragment_program_gxm.renderer_data=std::make_unique<renderer::ShaderProgram>();
 vertex_program_gxm.renderer_data=std::make_unique<renderer::ShaderProgram>();
 auto make=[](bool fragment){SceGxmProgram p{};p.magic=0x00505847;p.size=sizeof(p);p.program_flags=fragment?SCE_GXM_PROGRAM_FLAG_FRAGMENT:0;p.default_uniform_buffer_count=fragment?9:7;auto b=std::make_shared<std::vector<uint8_t>>(sizeof(p));memcpy(b->data(),&p,sizeof(p));return b;};
 auto fp=make(true),vp=make(false);
 fragment_program_gxm.renderer_data->program_source=fp;vertex_program_gxm.renderer_data->program_source=vp;
 memset(mem.bytes+128,0xA5,sizeof(SceGxmProgram));memset(mem.bytes+1024,0xA5,sizeof(SceGxmProgram));
 check(is_sane_fragment_program(mem,&fragment_program_gxm),"poisoned guest fragment can rebind using retained GXP");
 check(is_sane_vertex_program(mem,&vertex_program_gxm),"poisoned guest vertex can rebind using retained GXP");
'''
if accessor:
    cpp += r'''
 auto fragment_source=renderer::capture_program_source(fragment_program_gxm,mem);
 auto vertex_source=renderer::capture_program_source(vertex_program_gxm,mem);
'''
cpp += f' const SceGxmProgram *selected_fragment = {fragment_expression};\n const SceGxmProgram *selected_vertex = {vertex_expression};\n'
cpp += r'''
 check(selected_fragment==reinterpret_cast<const SceGxmProgram*>(fp->data()),"uncached pipeline selects retained fragment input");
 check(selected_vertex==reinterpret_cast<const SceGxmProgram*>(vp->data()),"uncached pipeline selects retained vertex input");
 check(selected_fragment->default_uniform_buffer_count==9&&selected_vertex->default_uniform_buffer_count==7,"pipeline input retains stage-specific uniform metadata");
'''
if accessor:
    cpp += r'''
 EmuEnvState env{mem};SceGxmShaderPatcher patcher;
 auto *released_fragment=new(mem.bytes+2048)SceGxmFragmentProgram;
 released_fragment->renderer_data=std::make_unique<renderer::ShaderProgram>();
 released_fragment->renderer_data->program_source=make(true);
 std::weak_ptr<const std::vector<uint8_t>> released_fp=released_fragment->renderer_data->program_source;
 env.gxm.fragment_programs[2048]=released_fragment->renderer_data->program_source;
 sceGxmShaderPatcherReleaseFragmentProgram(env,0,"test",&patcher,Ptr<SceGxmFragmentProgram>{2048});
 check(released_fp.expired(),"actual fragment release drops source before guest wrapper memory is reused");
 released_fragment->~SceGxmFragmentProgram();
 auto *released_vertex=new(mem.bytes+2048)SceGxmVertexProgram;
 released_vertex->renderer_data=std::make_unique<renderer::ShaderProgram>();
 released_vertex->renderer_data->program_source=make(false);
 std::weak_ptr<const std::vector<uint8_t>> released_vp=released_vertex->renderer_data->program_source;
 env.gxm.vertex_programs[2048]=released_vertex->renderer_data->program_source;
 sceGxmShaderPatcherReleaseVertexProgram(env,0,"test",&patcher,Ptr<SceGxmVertexProgram>{2048});
 check(released_vp.expired(),"actual vertex release drops source before guest wrapper memory is reused");
 released_vertex->~SceGxmVertexProgram();
 std::weak_ptr<const std::vector<uint8_t>> weak_fp=fp,weak_vp=vp;
 fragment_program_gxm.renderer_data.reset();vertex_program_gxm.renderer_data.reset();fp.reset();vp.reset();
 check(!weak_fp.expired()&&!weak_vp.expired(),"async captured inputs retain both owners after wrapper data is released");
 check(fragment_source.get()->magic==0x00505847&&vertex_source.get()->is_vertex(),"async input remains valid without GXM map or wrapper lookup");
 fragment_source={};vertex_source={};check(weak_fp.expired()&&weak_vp.expired(),"async completion releases source owners");
 SceGxmProgram valid{};valid.magic=0x00505847;valid.size=sizeof(valid);
 memcpy(mem.bytes+1024,&valid,sizeof(valid));
 check(renderer::get_program(vertex_program_gxm,mem)==vertex_program_gxm.program.get(mem),"legacy wrapper falls back to original guest pointer");
 check(renderer::get_program(fragment_program_gxm,mem)==fragment_program_gxm.program.get(mem),"legacy fragment falls back to original guest pointer");
'''
cpp += ' return failed?1:0;\n}\n'
(out / 'probe.cpp').write_text(cpp, encoding='utf-8')
compiler = os.environ.get('CXX') or shutil.which('g++') or 'C:/msys64/ucrt64/bin/g++.exe'
env = os.environ.copy()
env['PATH'] = str(Path(compiler).parent) + os.pathsep + env['PATH']
subprocess.run([compiler, '-std=c++20', '-O0', '-I'+str(native/'gxm/include'), '-I'+str(native/'util/include'),
                str(out/'probe.cpp'), '-o', str(out/'probe.exe')], check=True, env=env)
result = subprocess.run([str(out/'probe.exe')], env=env, capture_output=True, text=True, timeout=15)
print(result.stdout, end='')
(out/'result.log').write_text(result.stdout+result.stderr, encoding='utf-8')
raise SystemExit(result.returncode)
