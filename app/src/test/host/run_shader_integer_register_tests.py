#!/usr/bin/env python3
"""Evaluate the production index-store and IMAD16 emission with numeric Builder seams.

This checks emitted arithmetic/lanes, not SPIR-V validation or undocumented SGX saturation.
"""
from pathlib import Path
import os, shutil, subprocess, sys

root = Path(__file__).resolve().parents[4]
core = root / 'app/src/main/cpp/vita3k/vita3k'
out = root / 'app/build/shader-integer-register-probe'
out.mkdir(parents=True, exist_ok=True)

def read(name):
    path = core / name
    return (subprocess.check_output(['git', 'show', 'HEAD:' + path.relative_to(root).as_posix()], cwd=root, text=True)
            if '--baseline' in sys.argv else path.read_text(encoding='utf-8'))

def block(source, marker):
    start = source.index(marker)
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if not depth:
            return source[start:end + 1]
    raise AssertionError(marker)

store_source = read('shader/src/usse_utilities.cpp')
index_store = block(store_source[store_source.index('void store('):], '        if (dest.bank == RegisterBank::INDEX) {')
ialu = block(read('shader/src/translator/ialu.cpp'), 'bool USSETranslatorVisitor::i16mad(')
imad = ialu[ialu.index('    inst.opr.src0.swizzle ='):ialu.index('    END_REPEAT();')]

cpp = r'''
#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>
enum class DataType {UINT8,INT8,UINT16,INT16,UINT32,F32};
enum class RegisterBank {TEMP,INDEX,FPINTERNAL,IMMEDIATE};
int get_data_type_size(DataType t){return t==DataType::UINT8||t==DataType::INT8?1:t==DataType::UINT16||t==DataType::INT16?2:4;}
bool is_float_data_type(DataType t){return t==DataType::F32;}
bool is_signed_integer_data_type(DataType t){return t==DataType::INT8||t==DataType::INT16;}
constexpr int SWIZZLE_CHANNEL_4_DEFAULT=0;
struct Operand {RegisterBank bank=RegisterBank::TEMP;DataType type=DataType::UINT16;int num=1,swizzle=0;uint32_t value=0;};
namespace spv {
using Id=uint64_t;constexpr Id NoResult=UINT64_MAX;
enum {OpBitcast,OpBitwiseAnd,OpIMul,OpIAdd,OpISub};
struct Builder {
 bool integer_source=true;
 bool isIntType(Id){return integer_source;}Id makeIntType(int){return 0;}Id makeIntConstant(int n){return uint32_t(n);}Id makeUintConstant(uint32_t n){return n;}
 Id createOp(int op,Id,const std::vector<Id>&a){return a[0];}
 Id createBinOp(int op,Id,Id a,Id b){switch(op){case OpBitwiseAnd:return uint32_t(a)&uint32_t(b);case OpIMul:return uint32_t(a*b);case OpISub:return uint32_t(a-b);default:return uint32_t(a+b);}}
 Id getTypeId(Id){return 0;}Id makeVectorType(Id,int){return 0;}
 Id createCompositeConstruct(Id,const std::vector<Id>&a){return uint32_t(a[0])|(Id(uint32_t(a[1]))<<32);}
};
}
uint32_t index_write(uint32_t value,DataType type){spv::Builder b;b.integer_source=type!=DataType::F32;Operand dest;dest.bank=RegisterBank::INDEX;dest.type=type;spv::Id source=value;
''' + index_store + r'''
return uint32_t(source);}
struct Probe {
 spv::Builder m_b;struct {struct {Operand src0,src1,src2,dest;}opr;}inst;
 int src0_repeat_offset=0,src1_repeat_offset=0,src2_repeat_offset=0,dest_repeat_offset=0;bool src2_neg=false;uint8_t mask_src1=1,mask_src2=1;uint32_t result=0;unsigned write_mask=0;
 spv::Id load(const Operand&op,uint8_t mask,int){int lane=0;while(!(mask&(1<<lane)))++lane;const int bits=get_data_type_size(op.type)*8;uint32_t v=op.bank==RegisterBank::IMMEDIATE?op.value:(op.value>>(lane*bits));
  if(bits==8)return op.type==DataType::INT8?uint32_t(int8_t(v)):uint8_t(v);
  if(bits==16)return op.type==DataType::INT16?uint32_t(int16_t(v)):uint16_t(v);return v;}
 void store(const Operand&,spv::Id value,unsigned mask,int){write_mask=mask;result=uint16_t(value);if(mask==3)result|=uint32_t(uint16_t(value>>32))<<16;}
 void run(){
''' + imad + r'''
 }
};
int main(){int failures=0,checks=0;auto check=[&](bool ok,const char*name){++checks;if(!ok){++failures;printf("FAIL %s\n",name);}};
 for(auto type:{DataType::UINT16,DataType::INT16,DataType::UINT32,DataType::F32}){
  for(uint32_t value:{0u,0xffffu,0x10000u,0x12345678u,0xffffffffu})check(index_write(value,type)==(value&0xffffu),"index register always stores its low 16 bits");
 }
 check(index_write(0x12345678,DataType::UINT8)==0x78,"byte index write remains truncated");
 Probe p;p.inst.opr.src0.value=0x00030002;p.inst.opr.src1.value=0x00050004;p.inst.opr.src2.value=0x00070006;p.run();check(p.result==0x0016000e,"IMAD16 writes both packed lanes");
 p.src2_neg=true;p.run();check(p.result==0x00080002,"IMAD16 subtracts src2 when requested");p.src2_neg=false;
 p.inst.opr.dest.bank=RegisterBank::INDEX;p.run();check(p.result==14&&p.write_mask==1,"index destination has one lane");p.inst.opr.dest.bank=RegisterBank::TEMP;
 p.inst.opr.src1.type=DataType::UINT8;p.mask_src1=2;p.inst.opr.src1.value=0x09050704;p.run();check(p.result==0x00220014,"upper-byte selection is applied separately in both halves");
 p.inst.opr.src1.type=DataType::UINT16;p.mask_src1=1;p.inst.opr.src1.bank=RegisterBank::IMMEDIATE;p.inst.opr.src1.value=4;p.run();check(p.result==0x0007000e,"immediate operand has zero upper half");
 p.inst.opr.src1.bank=RegisterBank::TEMP;p.inst.opr.src0.type=p.inst.opr.src1.type=p.inst.opr.src2.type=DataType::INT16;
 p.inst.opr.src0.value=0xfffdfffe;p.inst.opr.src1.value=0x00050004;p.inst.opr.src2.value=0x00070006;p.run();check(p.result==0xfff8fffe,"signed packed lanes preserve negative results");
 printf("%d checks, %d failures\n",checks,failures);return failures?1:0;}
'''
(out / 'probe.cpp').write_text(cpp, encoding='utf-8')
if os.name == 'nt' and Path('C:/msys64/ucrt64/bin').is_dir():
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler, 'A host C++20 compiler is required'
subprocess.run([compiler, '-std=c++20', '-O0', str(out / 'probe.cpp'), '-o', str(out / 'probe.exe')], check=True)
result = subprocess.run([str(out / 'probe.exe')], capture_output=True, text=True, timeout=10)
print(result.stdout + result.stderr)
(out / ('red.log' if result.returncode else 'green.log')).write_text(result.stdout + result.stderr)
raise SystemExit(result.returncode)
