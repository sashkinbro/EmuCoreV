#!/usr/bin/env python3
"""Run production FPCONSTANT loads against a typed Builder seam and real tables.

Checks emitted constant types/values, not a complete SPIR-V module or SGX hardware.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
source_path = root / 'app/src/main/cpp/vita3k/vita3k/shader/src/usse_utilities.cpp'
out = root / 'app/build/shader-fpconstant-probe'
out.mkdir(parents=True, exist_ok=True)
parser = argparse.ArgumentParser()
parser.add_argument('--baseline', action='store_true')
args = parser.parse_args()
source = (subprocess.check_output(['git', 'show', 'HEAD:' + source_path.relative_to(root).as_posix()], cwd=root, text=True)
          if args.baseline else source_path.read_text())
start = source.index('    if (op.bank == RegisterBank::FPCONSTANT) {', source.index('spv::Id load('))
opening = source.index('{', start)
depth = 0
for end in range(opening, len(source)):
    depth += (source[end] == '{') - (source[end] == '}')
    if depth == 0:
        block = source[start:end + 1]
        break
else:
    raise AssertionError('FPCONSTANT block')

cpp = r'''
#include <shader/usse_constant_table.h>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>
namespace usse = shader::usse;
enum class DataType {F32,F16,UINT32,INT32,UINT16,INT16};
enum class RegisterBank {FPCONSTANT};
enum class SwizzleChannel {C_X,C_Y,C_Z,C_W,C_0,C_1,C_2,C_H};
struct Operand {RegisterBank bank=RegisterBank::FPCONSTANT;DataType type;int num;SwizzleChannel swizzle[4];int flags=0;};
using Imm4 = unsigned;
namespace util {uint16_t encode_flt16(float value){return std::bit_cast<uint16_t>(static_cast<_Float16>(value));}}
namespace spv {
using Id=unsigned;constexpr Id NoResult=0;
struct Builder {
 struct Value {Id type;std::vector<uint32_t> bits;};
 std::map<Id,Value> values;std::map<Id,std::pair<Id,int>> types{{1,{1,1}},{2,{2,1}},{3,{3,1}}};Id next=100;int invalid=0;
 Id makeFloatType(int){return 1;}Id makeUintType(int){return 2;}Id makeIntType(int){return 3;}
 Id makeVectorType(Id scalar,int n){auto id=next++;types[id]={scalar,n};return id;}
 Id value(Id type,std::vector<uint32_t> bits){auto id=next++;values[id]={type,bits};return id;}
 Id makeFloatConstant(float v){return value(1,{std::bit_cast<uint32_t>(v)});}
 Id makeUintConstant(uint32_t v){return value(2,{v});}Id makeIntConstant(int32_t v){return value(3,{uint32_t(v)});}
 Id getTypeId(Id v){return values.at(v).type;}
 Id makeCompositeConstant(Id type,const std::vector<Id>& constituents){
  std::vector<uint32_t> bits;auto [scalar,n]=types.at(type);if(n!=int(constituents.size()))++invalid;
  for(auto c:constituents){if(getTypeId(c)!=scalar)++invalid;bits.push_back(values.at(c).bits[0]);}return value(type,bits);
 }
};
}
struct SpirvUtilFunctions {};
spv::Id apply_modifiers(spv::Builder&,SpirvUtilFunctions&,int,spv::Id value){return value;}
spv::Id load_constant(spv::Builder& b,SpirvUtilFunctions& utils,Operand op,Imm4 dest_mask){
 auto type_f32=b.makeFloatType(32);
''' + block + r'''
 return spv::NoResult;
}
int checks=0,failures=0;
void check(bool condition,const char* message){++checks;if(!condition){++failures;if(failures<=12)printf("FAIL %s\n",message);}}
uint32_t expected(DataType type,int num,SwizzleChannel ch){
 bool floating=type==DataType::F32||type==DataType::F16;
 if(ch>=SwizzleChannel::C_0){float v=ch==SwizzleChannel::C_0?0:ch==SwizzleChannel::C_1?1:ch==SwizzleChannel::C_2?2:0.5f;return floating?std::bit_cast<uint32_t>(v):uint32_t(v);}
 int bank=int(ch);
 if(type==DataType::F32||type==DataType::UINT32||type==DataType::INT32)
  return bank==1?usse::f32_constant_table_bank_1_raw[num]:usse::f32_constant_table_bank_0_raw[num];
 const float* tables[]={usse::f16_constant_table_bank0,usse::f16_constant_table_bank1,usse::f16_constant_table_bank2,usse::f16_constant_table_bank3};
 float v=tables[bank][num];auto half=util::encode_flt16(v);
 return floating?std::bit_cast<uint32_t>(v):type==DataType::INT16?uint32_t(int32_t(int16_t(half))):half;
}
int main(){
 const SwizzleChannel swizzles[][4]={{SwizzleChannel::C_X,SwizzleChannel::C_Y,SwizzleChannel::C_Z,SwizzleChannel::C_W},
 {SwizzleChannel::C_W,SwizzleChannel::C_Z,SwizzleChannel::C_Y,SwizzleChannel::C_X},
 {SwizzleChannel::C_0,SwizzleChannel::C_1,SwizzleChannel::C_2,SwizzleChannel::C_H}};
 for(auto type:{DataType::F32,DataType::F16,DataType::UINT32,DataType::INT32,DataType::UINT16,DataType::INT16})
 for(int num:{12,16})for(auto& swizzle:swizzles)for(unsigned mask:{1,2,3,5,15}){
  spv::Builder b;SpirvUtilFunctions utils;Operand op;op.type=type;op.num=num;for(int i=0;i<4;++i)op.swizzle[i]=swizzle[i];
  auto result=load_constant(b,utils,op,mask);auto value=b.values.at(result);auto [scalar,count]=b.types.at(value.type);
  int wanted=type==DataType::F32||type==DataType::F16?1:type==DataType::UINT32||type==DataType::UINT16?2:3;
  check(b.invalid==0,"constant composite constituents match declared scalar type");check(scalar==unsigned(wanted),"loaded constant has operand scalar type");
  std::vector<uint32_t> wanted_bits;for(int i=0;i<4;++i)if(mask&(1<<i))wanted_bits.push_back(expected(type,num,swizzle[i]));
  check(value.bits==wanted_bits,"constant lanes preserve table bits and mask/swizzle order");check(count==int(wanted_bits.size()),"component count follows destination mask");
 }
 printf("%d checks, %d failures\n",checks,failures);return !!failures;
}
'''
(out / 'probe.cpp').write_text(cpp)
if os.name == 'nt' and Path('C:/msys64/ucrt64/bin').is_dir():
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler
subprocess.run([compiler, '-std=c++20', '-O0', '-I', str(root / 'app/src/main/cpp/vita3k/vita3k/shader/include'), str(out / 'probe.cpp'), '-o', str(out / 'probe.exe')], check=True)
result = subprocess.run([str(out / 'probe.exe')], capture_output=True, text=True, timeout=10)
print(result.stdout + result.stderr)
(out / ('red.log' if result.returncode else 'green.log')).write_text(result.stdout + result.stderr)
raise SystemExit(result.returncode)
