#!/usr/bin/env python3
"""Exercise the actual asynchronous pipeline request snapshot without a GPU.

The GxmRecordState declaration, CompileRequest and queue initialization are
extracted unchanged. Driver handles, guest pointers and surface payloads have
small value-type seams; no pipeline compilation/snapshot logic is duplicated.
The old prefix allocation is checked before reading the omitted surface, so RED
reports the invalid request extent without invoking its out-of-bounds access.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[4]
out = root / 'app/build/pipeline-compile-snapshot-probe'
out.mkdir(parents=True, exist_ok=True)
path = 'app/src/main/cpp/vita3k/vita3k/renderer/src/vulkan/pipeline_cache.cpp'
source = ((root / path).read_text() if '--baseline' not in sys.argv else
          subprocess.check_output(['git', 'show', 'HEAD:' + path], cwd=root, text=True))
types = (root / 'app/src/main/cpp/vita3k/vita3k/renderer/include/renderer/types.h').read_text()


def block(text, marker):
    assert text.count(marker) == 1, marker
    start = text.index(marker)
    opening = text.index('{', start)
    depth = 0
    for end in range(opening, len(text)):
        depth += (text[end] == '{') - (text[end] == '}')
        if not depth:
            return text[start:end + 1]
    raise AssertionError(marker)


cpp = r'''
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <gxm/types.h>
template<class T> struct Ptr {
 uint32_t addr=0; Ptr()=default; explicit Ptr(uint32_t v):addr(v){}
 uint32_t address()const{return addr;} explicit operator bool()const{return addr!=0;}
};
using Sha256Hash=std::array<uint8_t,32>;
struct SceGxmColorSurface {Ptr<void> data; SceGxmColorFormat colorFormat;};
struct SceGxmDepthStencilSurface {uint32_t value=0;};
struct SceGxmVertexProgram {}; struct SceGxmFragmentProgram {};
namespace vk {using Pipeline=uint64_t;using RenderPass=uint64_t;}
namespace shader {struct Hints {uint32_t value=0;};}
'''
for name in ('GXMStreamInfo', 'GxmStencilStateOp', 'GxmStencilStateValues', 'GxmRecordState'):
    cpp += block(types, 'struct ' + name + ' {') + ';\n'
cpp += 'constexpr size_t record_pipeline_len = offsetof(GxmRecordState, vertex_streams);\n'
cpp += block(source, 'struct CompileRequest {') + ';\n'
begin = source.index('        CompileRequest *request = new CompileRequest;')
end = source.index('        it->second = pipeline_compiling;', begin)
cpp += r'''
CompileRequest* queued_snapshot(const GxmRecordState& record) {
 struct {vk::Pipeline second;} slot{};auto it=&slot;
 SceGxmPrimitiveType type=SCE_GXM_PRIMITIVE_TRIANGLES;vk::RenderPass render_pass=19;
 SceGxmVertexProgram vertex_program_gxm;SceGxmFragmentProgram fragment_program_gxm;
 struct {shader::Hints shader_hints;} context{{41}};
'''
cpp += source[begin:end] + '\nreturn request;\n}\n'
# Exact extent is a safety prerequisite: get_record() must designate a complete
# live object before the consumer may inspect color_surface beyond the prefix.
storage = 'sizeof(request->record_data)' if 'uint8_t record_data[' in source else 'sizeof(request->record)'
cpp += r'''
int failures=0,checks=0;
void check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
void run_case(SceGxmColorBaseFormat format,uint32_t address) {
 GxmRecordState original{};original.color_base_format=format;
 original.color_surface.data=Ptr<void>(address);
 original.color_surface.colorFormat=SCE_GXM_COLOR_FORMAT_F16F16F16F16_ABGR;
 original.vertex_streams[3].data=Ptr<const uint8_t>(0x81008000);
 original.line_width=7;
 auto* request=queued_snapshot(original);
 original.color_base_format=SCE_GXM_COLOR_BASE_FORMAT_U8U8U8U8;
 original.color_surface.data=Ptr<void>(0);original.line_width=99;
 check(request->get_record()->color_base_format==format,"queued format is independent of producer mutation");
 bool complete=STORAGE_SIZE>=sizeof(GxmRecordState);
 check(complete,"async request owns complete consumer record, including color surface");
 if(complete){
  const auto& saved=*request->get_record();
  check(saved.color_surface.data.address()==address,"queued surface address survives producer mutation");
  check(saved.color_surface.colorFormat==SCE_GXM_COLOR_FORMAT_F16F16F16F16_ABGR,"queued surface format survives");
  check(saved.vertex_streams[3].data.address()==0x81008000,"record tail copied by value");
  check(saved.line_width==7,"record tail independent of later producer changes");
  const bool raw=saved.color_base_format==SCE_GXM_COLOR_BASE_FORMAT_F16F16F16F16&&saved.color_surface.data;
  check(raw==(format==SCE_GXM_COLOR_BASE_FORMAT_F16F16F16F16&&address!=0),"F16 attachment decision consumes owned surface");
 }
 delete request;
}
int main(){
 static_assert(std::is_trivially_copyable_v<GxmRecordState>);
 run_case(SCE_GXM_COLOR_BASE_FORMAT_F16F16F16F16,0x81004000);
 run_case(SCE_GXM_COLOR_BASE_FORMAT_F16F16F16F16,0);
 run_case(SCE_GXM_COLOR_BASE_FORMAT_U8U8U8U8,0x81004000);
 std::printf("%s actual pipeline request snapshot: %d checks, %d failures\n",failures?"FAIL":"PASS",checks,failures);
 return !!failures;
}
'''.replace('STORAGE_SIZE', storage)
(out / 'probe.cpp').write_text(cpp)
if os.name == 'nt' and Path('C:/msys64/ucrt64/bin').is_dir():
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler, 'A C++20 compiler is required'
subprocess.run([compiler, '-std=c++20', '-O0',
                '-I' + str(root / 'app/src/main/cpp/vita3k/vita3k/gxm/include'),
                '-I' + str(root / 'app/src/main/cpp/vita3k/vita3k/util/include'),
                str(out / 'probe.cpp'), '-o', str(out / 'probe.exe')], check=True)
result = subprocess.run([str(out / 'probe.exe')], capture_output=True, text=True, timeout=10)
print(result.stdout + result.stderr)
(out / ('red.log' if result.returncode else 'green.log')).write_text(result.stdout + result.stderr)
raise SystemExit(result.returncode)
