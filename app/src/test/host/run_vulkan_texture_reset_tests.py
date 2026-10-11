#!/usr/bin/env python3
"""Compile exact texture lifecycle bodies with a bounded allocation/driver seam.

The Android shared-library executable cannot initialize the system Vulkan loader:
adrenotools exports a symbol that interposes a platform namespace function. This
host regression replaces Vulkan/VMA calls only, preserving all production cleanup,
reset, staging-selection and sampler-indexing decisions. Allocation generations
detect use after release without actually dereferencing freed memory.
"""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
out = root / 'app/build/vulkan-texture-reset-probe'
out.mkdir(parents=True, exist_ok=True)
source = (root / 'app/src/main/cpp/vita3k/vita3k/renderer/src/vulkan/texture.cpp').read_text()


def body(marker):
    assert source.count(marker) == 1, f'production marker changed: {marker}'
    start = source.index(marker)
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return source[start:end + 1]
    raise AssertionError(f'unterminated production body: {marker}')


cpp = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>
#define LOG_ERROR(...) ((void)0)
template<class T> T align(T value, unsigned n) { return (value+n-1)/n*n; }
namespace vk {
using DeviceSize=uint64_t;
struct Handle {
 int value=0; Handle()=default; Handle(int v):value(v){} Handle(std::nullptr_t){}
 Handle& operator=(std::nullptr_t){value=0;return *this;}
 explicit operator bool()const{return value!=0;}
 bool operator==(const Handle&)const=default;
};
using Fence=Handle; using Sampler=Handle;
enum class Result {eSuccess};
enum class BufferUsageFlagBits {eTransferSrc};
enum class CommandBufferUsageFlagBits {eOneTimeSubmit};
enum class ImageAspectFlagBits {eColor};
enum class Filter {eNearest,eLinear};
enum class SamplerMipmapMode {eLinear,eNearest};
enum class SamplerAddressMode {eRepeat};
struct CommandBufferBeginInfo {CommandBufferUsageFlagBits flags;};
struct CommandBuffer {CommandBuffer& operator=(std::nullptr_t){return *this;} void end(){} void begin(CommandBufferBeginInfo){} };
struct SubmitInfo {void setCommandBuffers(const std::vector<CommandBuffer>&){} };
struct ImageSubresourceRange {ImageAspectFlagBits aspectMask;uint32_t baseMipLevel,levelCount,baseArrayLayer,layerCount;};
struct SamplerCreateInfo {
 Filter magFilter,minFilter;SamplerMipmapMode mipmapMode;
 SamplerAddressMode addressModeU,addressModeV,addressModeW;
 float mipLodBias,maxAnisotropy;int compareEnable;float minLod,maxLod;int unnormalizedCoordinates;bool anisotropyEnable;
};
}
constexpr int VK_FALSE=0; constexpr float VK_LOD_CLAMP_NONE=1000;
using SceGxmTextureAddrMode=int;
enum SceGxmTextureFilter {SCE_GXM_TEXTURE_FILTER_POINT};
constexpr int SCE_GXM_TEXTURE_LINEAR_STRIDED=1;
struct SceGxmTexture {
 int uaddr_mode=0,vaddr_mode=0,mag_filter=0,min_filter=0,mip_filter=0,lod_bias=31;
 int texture_type()const{return 0;} int true_lod_min()const{return 0;}
};
namespace texture {
vk::Filter translate_filter(SceGxmTextureFilter){return vk::Filter::eNearest;}
vk::SamplerAddressMode translate_address_mode(SceGxmTextureAddrMode){return vk::SamplerAddressMode::eRepeat;}
}
struct Allocation {
 std::vector<uint8_t> bytes; bool live=true;
 explicit Allocation(size_t size):bytes(size){}
};
std::vector<std::unique_ptr<Allocation>> allocations;
namespace vkutil {
constexpr int vma_mapped_alloc=0;
enum class ImageLayout {Undefined,TransferDst,SampledImage};
struct Image {bool image=false;void destroy(){image=false;}};
struct Buffer {
 vk::Handle buffer;Allocation* allocation=nullptr;uint64_t size=0;void* mapped_data=nullptr;
 // Match the real Buffer::destroy contract: it clears only the live handle.
 void destroy(){if(buffer){allocation->live=false;buffer=nullptr;}}
 void init_buffer(vk::BufferUsageFlagBits,int){
  allocations.push_back(std::make_unique<Allocation>(size));allocation=allocations.back().get();
  buffer=vk::Handle(int(allocations.size()));mapped_data=allocation->bytes.data();
 }
 void write(const std::vector<uint8_t>& bytes){
  if(!buffer||!allocation||!allocation->live||!mapped_data||bytes.size()>size)
   throw std::runtime_error("texture upload reused a released staging allocation");
  std::memcpy(mapped_data,bytes.data(),bytes.size());
 }
};
void transition_image_layout(vk::CommandBuffer,bool,ImageLayout,ImageLayout,vk::ImageSubresourceRange){}
void transition_image_layout_discard(vk::CommandBuffer,bool,ImageLayout,ImageLayout,vk::ImageSubresourceRange){}
}
struct Device {
 std::set<int> live_samplers;int next=1;
 void destroy(vk::Sampler s){if(s&&!live_samplers.erase(s.value))throw std::runtime_error("double-destroyed sampler");}
 vk::Sampler createSampler(vk::SamplerCreateInfo){auto s=vk::Sampler(next++);live_samplers.insert(s.value);return s;}
 vk::Result waitForFences(vk::Fence,int,uint64_t){return vk::Result::eSuccess;}void resetFences(vk::Fence){}
};
struct VKContext {uint64_t frame_timestamp=1;vk::Fence next_fence;vk::CommandBuffer prerender_cmd;std::vector<vk::CommandBuffer> cmdbuffers_to_submit;};
struct VKState {void* context=nullptr;Device device;struct {void submit(vk::SubmitInfo,vk::Fence){}} general_queue;};
constexpr int NB_TEXTURE_STAGING_BUFFERS=2,TextureCacheSize=2;
bool is_frame_timestamp_in_flight(uint64_t a,uint64_t b){return a!=~uint64_t{0}&&a<=b&&b-a<3;}
struct Queue {
 struct Item {struct {int index=0;} content;};std::vector<Item> items;
 void init(size_t n){items.assign(n,{});}
};
struct TextureStagingBuffer {vkutil::Buffer buffer;uint32_t used_so_far=0;uint64_t scene_timestamp=~uint64_t{0},frame_timestamp=~uint64_t{0};vk::Fence waiting_fence;};
struct TextureCacheEntry {vkutil::Image texture;bool is_cube=false;uint16_t mip_count=1;uint32_t memory_needed=0;};
struct VKTextureCache {
 VKState& state;TextureStagingBuffer staging_buffers[NB_TEXTURE_STAGING_BUFFERS];
 uint32_t staging_idx=0;uint64_t last_waited_scene=0,current_scene_timestamp=1;
 std::array<TextureCacheEntry,TextureCacheSize> textures;std::vector<vk::Sampler> samplers;
 TextureCacheEntry* current_texture=nullptr;const SceGxmTexture* gxm_texture=nullptr;
 vk::CommandBuffer cmd_buffer;bool is_texture_transfer_ready=false;int anisotropic_filtering=1;
 std::map<int,int> texture_lookup,sampler_lookup,available_textures_hash,exported_textures_hash;
 Queue texture_queue,sampler_queue;void* current_info=nullptr;
 explicit VKTextureCache(VKState&s):state(s){}
 void cleanup();void reset();void prepare_staging_buffer(bool=false);
 void configure_sampler(size_t,const SceGxmTexture&,bool);
};
constexpr int VK_TRUE=1;
'''
for marker in ('void VKTextureCache::cleanup()', 'void VKTextureCache::reset()',
               'void VKTextureCache::prepare_staging_buffer(', 'void VKTextureCache::configure_sampler('):
    cpp += '\n' + body(marker) + '\n'
cpp += r'''
int failures=0;
void check(bool value,const char* label){if(!value){++failures;std::printf("FAIL %s\n",label);}}
void upload_after_reset(uint32_t old_size,uint32_t new_size){
 VKContext context;VKState state;state.context=&context;VKTextureCache cache(state);
 auto& old=cache.staging_buffers[1];old.buffer.size=old_size;
 old.buffer.init_buffer(vk::BufferUsageFlagBits::eTransferSrc,0);old.used_so_far=16;
 old.scene_timestamp=99;old.frame_timestamp=99;
 auto* previous=old.buffer.allocation;
 cache.reset();
 check(!previous->live,"reset releases old VMA allocation");
 check(old.buffer.size==0&&old.buffer.mapped_data==nullptr&&old.buffer.allocation==nullptr&&old.used_so_far==0,"reset invalidates staging capacity/mapping/cursor");
 for(int cycle=0;cycle<3;++cycle){
  cache.current_texture=&cache.textures[0];cache.current_texture->memory_needed=new_size;
  cache.current_texture->texture.image=true;cache.current_scene_timestamp=cycle+1;
  cache.prepare_staging_buffer(true);
  auto& selected=cache.staging_buffers[cache.staging_idx];
  std::vector<uint8_t> bytes(new_size,static_cast<uint8_t>(0x35+cycle));
  try{selected.buffer.write(bytes);check(selected.buffer.allocation->bytes==bytes,"post-reset upload preserves PCM-independent texture canary bytes");}
  catch(const std::exception&e){check(false,e.what());}
  cache.reset();
 }
}
void sampler_reuse(){
 VKState state;VKTextureCache cache(state);cache.sampler_queue.init(3);cache.samplers.resize(3);
 SceGxmTexture texture;
 for(int cycle=0;cycle<3;++cycle){
  for(size_t i=0;i<cache.samplers.size();++i)cache.configure_sampler(i,texture,true);
  check(state.device.live_samplers.size()==3,"normal sampler creation owns three handles");
  cache.reset();check(state.device.live_samplers.empty(),"reset releases all old samplers once");
  check(cache.samplers.size()==3&&cache.samplers.size()==cache.sampler_queue.items.size(),"sampler storage matches allocatable queue after reset");
  if(cache.samplers.size()!=3)break;
  for(auto s:cache.samplers)check(!s,"reset slots cannot double-destroy old sampler handles");
 }
 cache.cleanup();
}
int main(){upload_after_reset(4096,4096);upload_after_reset(4096,1024);sampler_reuse();
 std::printf("%s exact production texture reset/prepare/sampler bodies (%d failures)\n",failures?"FAIL":"PASS",failures);return !!failures;}
'''
(out / 'probe.cpp').write_text(cpp)
if os.name == 'nt' and Path('C:/msys64/ucrt64/bin').is_dir():
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler, 'A C++20 compiler is required'
subprocess.run([compiler, '-std=c++20', '-O0', str(out / 'probe.cpp'), '-o', str(out / 'probe.exe')], check=True)
result = subprocess.run([str(out / 'probe.exe')], capture_output=True, text=True, timeout=10)
print(result.stdout + result.stderr)
(out / ('red.log' if result.returncode else 'green.log')).write_text(result.stdout + result.stderr)
raise SystemExit(result.returncode)
