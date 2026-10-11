#!/usr/bin/env python3
"""Execute the production raw attachment descriptor/pool/subpass selectors.

Vulkan enum/container seams replace driver allocation. The expressions and pool
count statements come unchanged from the real producer, with all interlock,
raw-alias and mask combinations exercised. No Vulkan device is needed.
"""
from pathlib import Path
import os,re,subprocess,sys
root=Path(__file__).resolve().parents[4]
base='app/src/main/cpp/vita3k/vita3k/renderer/src/vulkan/'
def source(name):
 p=base+name
 return subprocess.check_output(['git','show','HEAD:'+p],cwd=root,text=True) if '--baseline' in sys.argv else (root/p).read_text()
pipeline=source('pipeline_cache.cpp');context=source('context.cpp')
raw_desc=re.search(r'\.binding = 2,\s*\.descriptorType = ([^,]+),',pipeline[pipeline.index('// raw u16x4 alias'):]).group(1)
subpass=re.search(r'subpass.inputAttachmentCount = ([^;]+);',pipeline[pipeline.index('// raw u16x4 alias'):]).group(1)
start=context.index('    uint32_t storage_images_per_set') if '    const uint32_t attachment_images_per_set' not in context else context.index('    const uint32_t attachment_images_per_set')
end=context.index('\n    vk::DescriptorPoolCreateInfo',start)
pool=context[start:end]
out=root/'app/build/raw-f16-descriptor-probe';out.mkdir(parents=True,exist_ok=True)
cpp=r'''
#include <cstdint>
#include <vector>
#include <cstdio>
namespace vk {enum class DescriptorType{eStorageImage,eInputAttachment};struct DescriptorPoolSize{DescriptorType type;uint32_t count;};}
struct Features {bool support_shader_interlock,preserve_f16_nan_as_u16,use_mask_bit,direct_fragcolor;};
struct State {Features features;};
constexpr uint32_t DESCRIPTOR_PACK_SIZE=16,MAX_FRAMES_RENDERING=3;
int failures=0,checks=0;
void check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
void run(bool interlock,bool raw,bool mask) {
 State state{{interlock,raw,mask,!interlock}};
 const auto intput_image_descriptor=interlock?vk::DescriptorType::eStorageImage:vk::DescriptorType::eInputAttachment;
'''+f' const auto raw_descriptor={raw_desc};\n'+pool+r'''
 uint32_t input=0,storage=0;
 for(const auto &p:pool_sizes){if(p.type==vk::DescriptorType::eInputAttachment)input=p.count;else storage=p.count;}
 check(input==(interlock?0u:(raw?2u:1u))*DESCRIPTOR_PACK_SIZE*MAX_FRAMES_RENDERING,"input attachment pool covers every alias");
 check(storage==((interlock?(raw?2u:1u):0u)+(mask?1u:0u))*DESCRIPTOR_PACK_SIZE*MAX_FRAMES_RENDERING,"storage pool owns only storage-image bindings");
 if(raw)check(raw_descriptor==intput_image_descriptor,"raw descriptor follows framebuffer-fetch transport");
 for(bool with_raw_attachment:{false,true}) {
'''+f' const uint32_t inputs={subpass};\n'+r'''
 check(inputs==((with_raw_attachment&&!interlock)?2u:1u),"subpass advertises the raw input only when physically attached");
 }
}
int main(){for(bool i:{false,true})for(bool r:{false,true})for(bool m:{false,true})run(i,r,m);std::printf("%d checks, %d failures\n",checks,failures);return failures?1:0;}
'''
(out/'probe.cpp').write_text(cpp)
gpp=Path('C:/msys64/ucrt64/bin/g++.exe');env=os.environ.copy();env['PATH']=str(gpp.parent)+';'+env.get('PATH','')
subprocess.run([str(gpp),'-std=c++20','-O0',str(out/'probe.cpp'),'-o',str(out/'probe.exe')],check=True,env=env)
raise SystemExit(subprocess.run([str(out/'probe.exe')],env=env).returncode)
