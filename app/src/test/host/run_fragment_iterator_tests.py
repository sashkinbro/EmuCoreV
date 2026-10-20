#!/usr/bin/env python3
"""Check production iterator emission and shader cache variant separation."""
from pathlib import Path
import os, shutil, subprocess

root = Path(__file__).resolve().parents[4]
core = root / 'app/src/main/cpp/vita3k/vita3k'
source = (core / 'shader/src/spirv_recompiler.cpp').read_text()
marker = 'static spv::Id create_fragment_iterator('
assert marker in source, 'missing compile-time specialization of fragment iterator interface'
start = source.index(marker)
helper = source[start:source.index('static void create_fragment_inputs(', start)]
variant = (core / 'renderer/include/renderer/shader_variant.h').read_text()
variant = variant[variant.index('namespace renderer {'):]
cpp = r'''
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
using Sha256Hash = std::array<uint8_t,32>;
enum SceGxmColorFormat { SCE_GXM_COLOR_FORMAT_U8_A, RGBA };
Sha256Hash sha256(const void *data, size_t size) {
    // Only the production variant tag encoding is under test here.
    Sha256Hash result{};
    const auto *bytes = static_cast<const uint8_t *>(data);
    for (size_t i=0; i<size; ++i) result[i%32] = (result[i%32]*33)^bytes[i];
    return result;
}
namespace spv {
using Id=uint32_t;
enum Decoration { NoPrecision, DecorationLocation, DecorationRelaxedPrecision };
enum StorageClass { StorageClassInput, StorageClassPrivate };
constexpr Id NoResult=0;
struct Builder {
    StorageClass storage=StorageClassInput;
    Id initializer=NoResult;
    std::vector<uint32_t> locations;
    std::vector<float> constants;
    Id makeFloatType(int) { return 1; }
    Id makeVectorType(Id, int) { return 2; }
    Id makeFloatConstant(float value) { constants.push_back(value); return 3; }
    Id makeCompositeConstant(Id, std::vector<Id> values) { return values.size()==4 ? 4 : 0; }
    Id createVariable(Decoration,StorageClass value,Id,const char*,Id init=NoResult) {
        storage=value;initializer=init;return 10;
    }
    void addDecoration(Id,Decoration,uint32_t location) { locations.push_back(location); }
};
}
struct Hints { uint32_t vertex_output_mask=0xffffffffu; };
struct TranslationState {
    bool is_vulkan=true;
    const Hints *hints;
    std::vector<spv::Id> interfaces;
};
''' + helper + variant + r'''
int main() {
    int failures=0;
    auto check=[&](bool ok,const char *name) { printf("%s %s\n",ok?"PASS":"FAIL",name);failures+=!ok; };
    auto emit=[&](uint32_t mask,uint32_t location,bool vulkan) {
        Hints hints{mask};TranslationState state{vulkan,&hints,{}};spv::Builder b;
        const auto id=create_fragment_iterator(b,state,spv::DecorationRelaxedPrecision,2,"iterator",location);
        const bool input=b.storage==spv::StorageClassInput;
        check(id==10,"iterator has a usable pointer");
        check(input ? state.interfaces==std::vector<spv::Id>{id} && b.locations==std::vector<uint32_t>{location}
                    : state.interfaces.empty() && b.locations.empty() && b.initializer!=spv::NoResult,
              "only real inputs enter the Vulkan interface; fallback is initialized private storage");
        if(!input) check(b.constants.size()>=2 && b.constants.front()==0.0f && b.constants.back()==1.0f,
                         "missing iterator uses guest fallback (0,0,0,1)");
        return input;
    };
    check(!emit(1u<<12,13,true),"DOA TexCoord9 missing from vertex output is not an input");
    check(!emit(1u<<12,9,true),"DOA TexCoord5 missing from vertex output is not an input");
    check(emit(1u<<12,12,true),"provided TexCoord8 remains live");
    check(emit(0xffffffffu,13,true),"unspecialized callers retain all inputs");
    check(emit(0,13,false),"OpenGL retains its existing dynamic mask behavior");
    Sha256Hash hash{};hash[0]=7;
    auto base=renderer::fragment_shader_variant_hash(hash,RGBA);
    check(base==hash,"unspecialized cache identity remains stable");
    auto a=renderer::fragment_shader_variant_hash(hash,RGBA,false,1u<<12);
    auto b=renderer::fragment_shader_variant_hash(hash,RGBA,false,(1u<<12)|(1u<<13));
    check(a!=base && a!=b,"different vertex interfaces cannot share fragment cache entry");
    check(a==renderer::fragment_shader_variant_hash(hash,RGBA,false,1u<<12),"variant key is deterministic");
    check(a!=renderer::fragment_shader_variant_hash(hash,RGBA,true,1u<<12),"raw attachment and iterator variants compose");
    check(a!=renderer::fragment_shader_variant_hash(hash,SCE_GXM_COLOR_FORMAT_U8_A,false,1u<<12),"alpha surface and iterator variants compose");
    return failures?1:0;
}
'''
out=root/'app/build/fragment-iterator-probe';out.mkdir(parents=True,exist_ok=True)
(out/'probe.cpp').write_text(cpp)
if os.name=='nt': os.environ['PATH']='C:/msys64/ucrt64/bin;'+os.environ['PATH']
compiler=os.environ.get('CXX') or shutil.which('g++');assert compiler
subprocess.run([compiler,'-std=c++20',str(out/'probe.cpp'),'-o',str(out/'probe.exe')],check=True)
result=subprocess.run([str(out/'probe.exe')],capture_output=True,text=True)
print(result.stdout+result.stderr,end='')
raise SystemExit(result.returncode)
