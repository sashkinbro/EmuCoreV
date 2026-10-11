#!/usr/bin/env python3
"""Exercise production uniform-register copy emission against scalar canaries.

This Builder seam checks emitted access/load/store/shuffle operations. It is not a
SPIR-V validator or GPU execution test. The mapped-copy seam records requested
scalar ranges without dereferencing guest memory.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
core = root / 'app/src/main/cpp/vita3k/vita3k'
out = root / 'app/build/shader-uniform-copy-probe'
out.mkdir(parents=True, exist_ok=True)
arguments = argparse.ArgumentParser()
arguments.add_argument('--baseline', action='store_true', help='Test HEAD production bodies without modifying the checkout')
options = arguments.parse_args()


def production(path):
    if options.baseline:
        return subprocess.check_output(['git', 'show', 'HEAD:' + path.relative_to(root).as_posix()], cwd=root, text=True)
    return path.read_text()


def extract(source, marker):
    assert source.count(marker) == 1, f'production marker changed: {marker}'
    start = source.index(marker)
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return source[start:end + 1]
    raise AssertionError(f'unterminated production body: {marker}')


shader = production(core / 'shader/src/spirv_recompiler.cpp')
copy = extract(shader, 'static void copy_uniform_block_to_register(')
scope_marker = 'spv_params.render_info_id = translation_state.render_info_id;'
assert shader.count(scope_marker) == 1
scope = shader[shader.index(scope_marker):shader.index('const auto add_var_to_reg', shader.index(scope_marker))]
dispatch = extract(scope, 'for (const auto &buffer : program_input.uniform_buffers)')
cpp = r'''
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <stdexcept>
#include <vector>
namespace spv {
using Id=unsigned;constexpr int StorageClassPrivate=1,StorageClassStorageBuffer=2,NoPrecision=0,NoResult=0,OpIAdd=1,OpVectorShuffle=2;
struct IdImmediate {bool id;unsigned value;};
struct Builder {
 int next=10; std::map<int,std::vector<int>> values;std::map<int,std::vector<int>> pointers;
 std::array<int,128> bank;std::vector<int> input;int writes=0,reads=0;
 Builder(){bank.fill(-777);for(int i=0;i<128;++i)input.push_back(1000+i);values[3]={0};}
 spv::Id value(std::vector<int> v){int id=next++;values[id]=std::move(v);return id;}
 spv::Id makeIntConstant(int n){return value({n});}
 spv::Id createLoad(int id,int){
  if(!pointers.contains(id))return value(values.at(id));
  auto p=pointers.at(id);int base=p.at(1)*4;
  if(p.size()==3){int index=base+p[2];++reads;if(p[0]==2)return value({input.at(index)});return value({bank.at(index)});}
  std::vector<int> v;for(int i=0;i<4;++i){++reads;v.push_back(p[0]==2?input.at(base+i):bank.at(base+i));}return value(v);
 }
 int getTypeId(int){return 0;}
 spv::Id createBinOp(int,int,int a,int b){return value({values.at(a)[0]+values.at(b)[0]});}
 spv::Id createOp(int,int,const std::vector<IdImmediate>& ops){
  std::vector<int> both=values.at(ops[0].value),v;auto second=values.at(ops[1].value);both.insert(both.end(),second.begin(),second.end());
  for(size_t i=2;i<ops.size();++i)v.push_back(both.at(ops[i].value));return value(v);
 }
 void createStore(int val,int ptr){auto p=pointers.at(ptr);auto v=values.at(val);int base=p[1]*4+(p.size()==3?p[2]:0);
  for(size_t i=0;i<v.size();++i){bank.at(base+i)=v[i];++writes;}
 }
};
}
namespace utils {
template<class F>void make_for_loop(spv::Builder&b,int ite,int begin,int end,F f){int first=b.values.at(begin)[0],last=b.values.at(end)[0];for(int i=first;i<last;++i){b.values[ite]={i};f();}}
int create_access_chain(spv::Builder&b,int,int root,std::initializer_list<spv::Id> indices){int id=b.next++;if(b.pointers.contains(root)){auto parent=b.pointers.at(root);if(parent!=std::vector<int>{2,0})throw std::runtime_error("unexpected storage-container shape");root=2;}std::vector<int> p{root};for(int v:indices)p.push_back(b.values.at(v)[0]);b.pointers[id]=p;return id;}
}
''' + copy + r'''
constexpr int REG_SA_COUNT=128,SCE_GXM_REAL_MAX_UNIFORM_BUFFER=15;
struct Uniform {uint32_t index,reg_start_offset,reg_block_size;};
struct Operand {uint16_t num;int bank,type;};
struct RegisterBank {static constexpr int SECATTR=1;};struct DataType {static constexpr int F32=1;};
struct Params {int uniforms=1,buffer_container=2;struct Info {int index_in_container=0;};std::map<int,Info> buffers{{0,{}}};};
uint32_t mapped_count=0,mapped_start=0;int mapped_calls=0;
namespace usse::utils {template<class F>void buffer_address_access(spv::Builder&,Params&,int,F,Operand dest,int,int,uint32_t,uint32_t count,int){mapped_count=count;mapped_start=dest.num;++mapped_calls;}}
void dispatch(spv::Builder&b,Uniform buffer,bool mapped){
 struct {std::vector<Uniform> uniform_buffers;}program_input{{buffer}};
 struct {bool enable_memory_mapping;}features{mapped};Params spv_params;int utils=0,ite_copy=3;
 auto convert_buffer_idx_to_host=[](int){return 0;};
''' + dispatch + r'''
}
int failures=0,checks=0;void check(bool good,const char*message){++checks;if(!good){++failures;std::printf("FAIL %s\n",message);}}
void copy_case(int start,int count){spv::Builder b;b.input.resize(count);bool safe=true;try{dispatch(b,{0,uint32_t(start),uint32_t(count)},false);}catch(const std::out_of_range&){safe=false;}
 check(safe,"uniform copy stays inside source and SA bank");
 int expected=std::min(count,128-start);check(std::count_if(b.bank.begin(),b.bank.end(),[](int v){return v!=-777;})==expected,"uniform copy writes exactly requested scalar tail");
 for(int i=0;i<128;++i)check(b.bank[i]==(i>=start&&i<start+expected?1000+i-start:-777),"uniform copy preserves neighboring SA canaries");}
int main(){
 for(int start=0;start<128;++start)for(int count:{0,1,3,4,5,8,128})copy_case(start,count);
 for(uint32_t start:{128u,129u,0xFFFFFFFFu}){spv::Builder b;mapped_calls=0;dispatch(b,{0,start,4},true);check(mapped_calls==0,"SA-start beyond bank must not emit physical buffer reads");}
 {spv::Builder b;mapped_calls=0;dispatch(b,{15,0,4},true);check(mapped_calls==0,"invalid uniform index must not emit buffer reads");}
 {spv::Builder b;mapped_calls=0;dispatch(b,{0,127,8},true);check(mapped_calls==1&&mapped_count==1&&mapped_start==127,"mapped copy clips last SA scalar");}
 std::printf("%s exact production uniform-copy body: %d checks, %d failures\n",failures?"FAIL":"PASS",checks,failures);return !!failures;
}
'''
(out / 'probe.cpp').write_text(cpp)
if os.name == 'nt' and Path('C:/msys64/ucrt64/bin').is_dir():
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler, 'A C++20 host compiler is required'
subprocess.run([compiler, '-std=c++20', '-O0', str(out / 'probe.cpp'), '-o', str(out / 'probe.exe')], check=True)
result = subprocess.run([str(out / 'probe.exe')], capture_output=True, text=True, timeout=10)
print(result.stdout + result.stderr)
(out / ('red.log' if result.returncode else 'green.log')).write_text(result.stdout + result.stderr)
raise SystemExit(result.returncode)
