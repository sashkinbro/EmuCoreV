#!/usr/bin/env python3
"""Execute production float-to-packed-integer emission with a numeric Builder seam.

The seam evaluates emitted conversions, clamp, multiplication and rounding. It
does not validate SPIR-V or claim native SGX rounding/NaN coverage.
"""
from pathlib import Path
import argparse, os, shutil, subprocess

root = Path(__file__).resolve().parents[4]
source_path = root / 'app/src/main/cpp/vita3k/vita3k/shader/src/usse_utilities.cpp'
out = root / 'app/build/shader-integer-pack-probe'
out.mkdir(parents=True, exist_ok=True)
parser = argparse.ArgumentParser()
parser.add_argument('--baseline', action='store_true')
args = parser.parse_args()
source = (subprocess.check_output(['git', 'show', 'HEAD:' + source_path.relative_to(root).as_posix()], cwd=root, text=True)
          if args.baseline else source_path.read_text())

def extract(marker):
    start = source.index(marker)
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return source[start:end + 1]
    raise AssertionError(marker)

cpp = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <vector>
enum class DataType {UINT8,INT8,UINT16,INT16,UINT32,INT32,C10};
bool is_unsigned_integer_data_type(DataType t){return t==DataType::UINT8||t==DataType::UINT16||t==DataType::UINT32;}
constexpr int GLSLstd450FClamp=1,GLSLstd450Round=2;
namespace spv {
using Id=unsigned;constexpr int OpFMul=1,OpConvertFToS=2,OpConvertFToU=3;
struct Builder {
 struct Value {std::vector<double> components;Id type;};
 std::map<Id,Value> values;std::map<Id,std::pair<Id,int>> types;Id next=100;int invalid=0;
 Builder(){types[1]={1,1};types[2]={2,1};types[3]={3,1};}
 Id makeFloatType(int){return 1;}Id makeUintType(int){return 2;}Id makeIntType(int){return 3;}
 Id makeVectorType(Id type,int count){Id id=next++;types[id]={type,count};return id;}
 Id value(std::vector<double> v,Id type){Id id=next++;values[id]={v,type};return id;}
 Id getTypeId(Id id){return values.at(id).type;}bool isVector(Id id){return isVectorType(getTypeId(id));}
 bool isVectorType(Id id){return types.at(id).second>1;}Id getContainedTypeId(Id id){return types.at(id).first;}
 bool isFloatType(Id id){return id==1;}int getNumComponents(Id id){return types.at(getTypeId(id)).second;}
 Id makeFloatConstant(float v){return value({v},1);}
 Id createBuiltinCall(Id type,int,int op,const std::vector<Id>& args){
  auto v=values.at(args[0]).components;
  for(size_t i=0;i<v.size();++i){if(op==GLSLstd450FClamp)v[i]=std::clamp(v[i],values.at(args[1]).components[i],values.at(args[2]).components[i]);else if(op==GLSLstd450Round)v[i]=std::round(v[i]);else assert(false);}
  return value(v,type);
 }
 Id createBinOp(int op,Id type,Id a,Id b){assert(op==OpFMul);auto v=values.at(a).components;for(size_t i=0;i<v.size();++i)v[i]*=values.at(b).components[i];return value(v,type);}
 Id createUnaryOp(int op,Id type,Id a){auto v=values.at(a).components;for(auto& f:v){double lo=op==OpConvertFToU?0:-2147483648.0,hi=op==OpConvertFToU?4294967295.0:2147483647.0;if(!std::isfinite(f)||f<lo||f>hi){++invalid;f=-999999;}else f=std::trunc(f);}return value(v,type);}
};
}
struct SpirvUtilFunctions {int std_builtins=0;};
spv::Id create_constant_vector_or_scalar(spv::Builder& b,spv::Id scalar,int count){return count==1?scalar:b.value(std::vector<double>(count,b.values.at(scalar).components[0]),b.makeVectorType(b.getTypeId(scalar),count));}
''' + extract('spv::Id unwrap_type(') + extract('static float get_int_normalize_range_constants(') + extract('spv::Id convert_to_int(') + r'''
int checks=0,failures=0;
void check(bool b,const char*message){++checks;if(!b){++failures;printf("FAIL %s\n",message);}}
void run(std::vector<double> input,std::vector<double> expected,DataType t,bool normal){
 spv::Builder b;SpirvUtilFunctions utils;int count=input.size();auto type=count==1?b.makeFloatType(32):b.makeVectorType(b.makeFloatType(32),count);
 auto result=convert_to_int(b,utils,b.value(input,type),t,normal);
 check(b.invalid==0,"packing never emits an out-of-range float-to-integer conversion");
 check(b.values.at(result).components==expected,"packed integer conversion produces expected saturated lanes");
}
int main(){
 for(double f:{-65504.0,-1.0,0.0,0.5,1.0,127.75,254.75,255.0,255.75,256.0,300.0,65504.0}){
  double expected=std::trunc(std::clamp(f,0.0,255.0));
  run({f},{expected},DataType::UINT8,false);
 }
 run({-1,100,256,65504},{0,100,255,255},DataType::UINT8,false);
 run({-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::infinity()},{0,255},DataType::UINT8,false);
 run({-1,0,0.5,2},{0,0,128,255},DataType::UINT8,true);
 run({-2,-0.5,0.5,2},{-127,-64,64,127},DataType::INT8,true);
 run({-127.5,126.5},{-127,126},DataType::INT8,false);
 run({256,65535},{256,65535},DataType::UINT16,false);
 run({65536,100000},{65536,100000},DataType::UINT32,false);
 printf("%d checks, %d failures\n",checks,failures);return !!failures;
}
'''
(out / 'probe.cpp').write_text(cpp)
if os.name == 'nt' and Path('C:/msys64/ucrt64/bin').is_dir():
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler
subprocess.run([compiler, '-std=c++20', '-O0', str(out / 'probe.cpp'), '-o', str(out / 'probe.exe')], check=True)
result = subprocess.run([str(out / 'probe.exe')], capture_output=True, text=True, timeout=10)
print(result.stdout + result.stderr)
(out / ('red.log' if result.returncode else 'green.log')).write_text(result.stdout + result.stderr)
raise SystemExit(result.returncode)
