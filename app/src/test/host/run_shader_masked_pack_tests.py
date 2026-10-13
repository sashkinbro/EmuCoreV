#!/usr/bin/env python3
"""Execute the production packed-store stage with numeric/bitwise Builder seams.

Half packing models the production finite clamp and permits NaN canonicalization;
neither behavior may change a lane that the guest instruction did not write.
"""
from pathlib import Path
import argparse, os, shutil, subprocess
root=Path(__file__).resolve().parents[4]
core=root/'app/src/main/cpp/vita3k/vita3k'
out=root/'app/build/shader-masked-pack-probe';out.mkdir(parents=True,exist_ok=True)
parser=argparse.ArgumentParser();parser.add_argument('--baseline',action='store_true');args=parser.parse_args()
path=core/'shader/src/usse_utilities.cpp'
text=(subprocess.check_output(['git','show','HEAD:'+path.relative_to(root).as_posix()],cwd=root,text=True) if args.baseline else path.read_text())
start=text.index('    if (dest.type != DataType::F32) {',text.index('void store('))
depth=0;opening=text.index('{',start)
for end in range(opening,len(text)):
    depth+=(text[end]=='{')-(text[end]=='}')
    if not depth:break
stage=text[start:end+1]
cpp=r'''
#include <util/float_to_half.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>
enum class DataType {F32,F16,UINT8,UINT16,INT8,INT16,C10};
int get_data_type_size(DataType t){return t==DataType::F16||t==DataType::UINT16||t==DataType::INT16?2:1;}
int get_packed_component_count(DataType t){return t==DataType::C10?3:4/get_data_type_size(t);}
namespace spv {
using Id=unsigned;constexpr Id NoResult=0;constexpr int NoPrecision=0;
enum {OpAccessChain,OpVectorExtractDynamic,OpBitcast,OpBitwiseAnd,OpBitwiseOr};
struct Builder {
 struct Value {std::vector<double> v;Id type;};std::map<Id,Value> values;std::map<Id,int> pointers;
 std::map<Id,std::pair<Id,int>> types{{1,{1,1}},{2,{2,1}},{3,{3,1}}};Id next=100;std::array<uint32_t,16> bank{};bool canonical=false;
 Id value(std::vector<double> v,Id t){Id id=next++;values[id]={v,t};return id;}
 Id makeFloatType(int){return 1;}Id makeUintType(int){return 2;}Id makeIntType(int){return 3;}
 Id makeIntConstant(int n){return value({double(n)},3);}Id makeUintConstant(uint32_t n){return value({double(n)},2);}Id makeFloatConstant(float n){return value({n},1);}
 Id makeVectorType(Id t,int n){Id id=next++;types[id]={t,n};return id;}
 Id getTypeId(Id id){return values.at(id).type;}Id getContainedTypeId(Id id){return types.at(id).first;}
 bool isFloatType(Id id){return id==1;}bool isUintType(Id id){return id==2;}
 bool isScalar(Id id){return values.at(id).v.size()==1;}Id createLoad(Id p,int){int base=pointers.at(p)*4;std::vector<double> v;for(int i=0;i<4;i++)v.push_back(bank.at(base+i));return value(v,makeVectorType(1,4));}
 Id createCompositeConstruct(Id t,const std::vector<Id>& a){std::vector<double> v;for(auto id:a){auto part=values.at(id).v;v.insert(v.end(),part.begin(),part.end());}return value(v,t);}
 Id createUnaryOp(int op,Id t,Id a){assert(op==OpBitcast);return value(values.at(a).v,t);}
 Id createBinOp(int op,Id t,Id a,Id b){auto v=values.at(a).v;for(size_t i=0;i<v.size();i++){uint32_t x=v[i],y=values.at(b).v[i];v[i]=op==OpBitwiseAnd?(x&y):(x|y);}return value(v,t);}
 Id createOp(int op,Id t,const std::vector<Id>& a){if(op==OpAccessChain){Id p=next++;pointers[p]=values.at(a[1]).v[0];return p;}if(op==OpVectorExtractDynamic)return value({values.at(a[0]).v.at(values.at(a[1]).v[0])},t);if(op==OpBitcast)return createUnaryOp(op,t,a[0]);assert(false);return 0;}
};
}
namespace utils {
spv::Id unwrap_type(spv::Builder&b,spv::Id t){return b.getContainedTypeId(t);}
spv::Id make_vector_or_scalar_type(spv::Builder&b,spv::Id t,int n){return n==1?t:b.makeVectorType(t,n);}
}
uint16_t half(double x,bool canonical){if(std::isnan(x))return canonical?0x7e00:util::encode_flt16(float(x));return util::encode_flt16(float(std::clamp(x,-65504.0,65504.0)));}
int bits(DataType t){return t==DataType::C10?10:get_data_type_size(t)*8;}
uint32_t encode(double x,DataType t,bool canonical){if(t==DataType::F16)return half(x,canonical);if(t==DataType::C10)return uint32_t(int32_t(std::round(std::clamp(x,-2.0,2.0)*255)))&1023;return uint32_t(int64_t(x))&((1u<<bits(t))-1);}
spv::Id unpack_one(spv::Builder&b,int,int,spv::Id raw,DataType t){uint32_t word=b.values.at(raw).v[0];std::vector<double> v;for(int i=0;i<get_packed_component_count(t);i++){uint32_t lane=(word>>(i*bits(t)))&((1u<<bits(t))-1);if(t==DataType::F16)v.push_back(util::decode_flt16<float>(uint16_t(lane)));else if(t==DataType::C10)v.push_back(double(int32_t(lane<<22)>>22)/255.0);else v.push_back(lane);}return b.value(v,b.makeVectorType(t==DataType::F16||t==DataType::C10?1:2,v.size()));}
spv::Id pack_one(spv::Builder&b,int,int,spv::Id data,DataType t){uint32_t word=0;auto v=b.values.at(data).v;for(size_t i=0;i<v.size();i++)word|=encode(v[i],t,b.canonical)<<(i*bits(t));return b.value({double(word)},1);}
struct Operand {DataType type;};
void packed_stage(spv::Builder&b,DataType t,int insert_offset,uint8_t dest_mask,std::vector<double> input){
 Operand dest{t};int num_comp_in_float=get_packed_component_count(t),nearest_swizz_on=0;
 if(dest_mask)while(!(dest_mask&(1<<nearest_swizz_on)))++nearest_swizz_on;
 nearest_swizz_on=nearest_swizz_on/num_comp_in_float*num_comp_in_float;
 int size_comp=get_data_type_size(t);spv::Id bank_base=9,comp_type=8,type_f32=1,elem=spv::NoResult;
 uint8_t total_comp_source=input.size();auto source=b.value(input,utils::make_vector_or_scalar_type(b,t==DataType::F16||t==DataType::C10?1:2,input.size()));
 int utils=0,features=0;
''' + stage + r'''
 auto values=b.values.at(source).v;int base=insert_offset+nearest_swizz_on/num_comp_in_float;
 for(size_t i=0;i<values.size();i++)b.bank.at(base+i)=uint32_t(values[i]);
}
int checks=0,failures=0;void check(bool v,const char*s){++checks;if(!v){++failures;if(failures<12)printf("FAIL %s\n",s);}}
int main(){
 for(auto t:{DataType::F16,DataType::UINT8,DataType::UINT16,DataType::INT8,DataType::INT16,DataType::C10})for(bool canonical:{false,true})for(int offset:{0,3,6})for(bool broadcast:{false,true})for(int mask=0;mask<16;mask++){
  spv::Builder b;b.canonical=canonical;
  std::array<uint32_t,8> patterns{0xff207c00,0xff20fc00,0x7e557d11,0xff804020,0x80000001,0x12345678,0xffff7c00,0xa55a5aa5};
  for(int i=0;i<16;i++)b.bank[i]=patterns[i%8];auto expected=b.bank;
  std::vector<double> source;int consumed=0,n=get_packed_component_count(t),width=bits(t);
  for(int lane=0;lane<4;lane++)if(mask&(1<<lane)){
   ++consumed;int component=broadcast?1:consumed;
   double value=t==DataType::F16?0.25*component:t==DataType::C10?0.125*component:double(17*component);source.push_back(value);
   int word=offset+lane/n;uint32_t bitmask=((1u<<width)-1)<<(width*(lane%n));
   expected[word]=(expected[word]&~bitmask)|(encode(value,t,canonical)<<(width*(lane%n)));
   if(t==DataType::C10)expected[word]&=0x3fffffff;
  }
  if(broadcast||!mask)source={t==DataType::F16?0.25:t==DataType::C10?0.125:17.0};
  packed_stage(b,t,offset,mask,source);
  for(int i=0;i<16;i++)check(b.bank[i]==expected[i],"masked packed store preserves every unaddressed bit and writes selected lanes");
 }
 printf("%d checks, %d failures\n",checks,failures);return !!failures;
}
'''
(out/'probe.cpp').write_text(cpp)
if os.name=='nt' and Path('C:/msys64/ucrt64/bin').is_dir():os.environ['PATH']='C:/msys64/ucrt64/bin;'+os.environ['PATH']
compiler=os.environ.get('CXX') or shutil.which('g++');assert compiler
subprocess.run([compiler,'-std=c++20','-O0','-I'+str(core/'util/include'),str(out/'probe.cpp'),'-o',str(out/'probe.exe')],check=True)
result=subprocess.run([str(out/'probe.exe')],capture_output=True,text=True,timeout=10)
print(result.stdout+result.stderr)
(out/('red.log' if result.returncode else 'green.log')).write_text(result.stdout+result.stderr)
raise SystemExit(result.returncode)
