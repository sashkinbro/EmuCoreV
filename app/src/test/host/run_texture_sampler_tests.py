#!/usr/bin/env python3
"""Exercise actual sampler cache and Vulkan sampler creation with a driver seam."""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
out = root / 'app/build/texture-sampler-probe'
out.mkdir(parents=True, exist_ok=True)

def body(path, marker):
    source = (root / path).read_text(encoding='utf-8')
    start = source.index(marker)
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return source[start:end + 1]
    raise AssertionError(marker)

gxm_types = body('app/src/main/cpp/emucorev/ECV/ECV/gxm/include/gxm/types.h', 'enum SceGxmTextureBaseFormat :') + ';\n'
gxm_types += body('app/src/main/cpp/emucorev/ECV/ECV/gxm/include/gxm/types.h', 'enum SceGxmTextureAddrMode :') + ';\n'
sampler_state = 'app/src/main/cpp/emucorev/ECV/ECV/renderer/include/renderer/sampler_state.h'
renderer_pieces = (body(sampler_state, 'struct SamplerSupport') + ';\n' + body(sampler_state, 'enum class AddressKind') + ';\n'
    + body(sampler_state, 'struct AddressTranslation') + ';\n' + body(sampler_state, 'inline AddressTranslation translate_address(') + '\n'
    + body(sampler_state, 'inline float translate_lod_bias(') + '\n' + body(sampler_state, 'struct AnisotropyPlan') + ';\n'
    + body(sampler_state, 'inline AnisotropyPlan plan_anisotropy(') + '\n')
cpp = r'''
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>
#define LOG_ERROR(...) do {} while (0)
#define LOG_WARN_ONCE(...) do {} while (0)
template <typename T> int log_hex(T) { return 0; }
constexpr int VK_TRUE=1;constexpr int VK_FALSE=0;constexpr float VK_LOD_CLAMP_NONE=1000;
enum SceGxmTextureFilter {SCE_GXM_TEXTURE_FILTER_POINT=0,SCE_GXM_TEXTURE_FILTER_LINEAR=1};
'''+ gxm_types + r'''
namespace renderer {
'''+ renderer_pieces + r'''
}
using SceGxmTextureFormat=uint32_t;
constexpr uint32_t SCE_GXM_TEXTURE_BASE_FORMAT_MASK=0x9f000000u;
constexpr int SCE_GXM_TEXTURE_LINEAR_STRIDED=1;
struct SceGxmTexture {
 int uaddr_mode=0,vaddr_mode=0,mag_filter=1,min_filter=1,mip_filter=1,lod_bias=31,lod_min0=0,lod_min1=0,type=0,normalize_mode=1;
 uint32_t base_format=12,format0=0,swizzle_format=0;
 void set_format(uint32_t f){base_format=(f>>24)&31;format0=f>>31;swizzle_format=(f>>12)&7;}
 int texture_type()const{return type;}int true_lod_min()const{return (lod_min0<<2)|lod_min1;}
};
namespace gxm {
'''+ body('app/src/main/cpp/emucorev/ECV/ECV/gxm/src/textures.cpp', 'SceGxmTextureFormat get_format(') + body('app/src/main/cpp/emucorev/ECV/ECV/gxm/src/textures.cpp', 'SceGxmTextureBaseFormat get_base_format(') + r'''
}
namespace vk {
enum class Filter {eNearest,eLinear};enum class SamplerMipmapMode {eNearest,eLinear};
enum class SamplerAddressMode {eRepeat,eMirroredRepeat,eClampToEdge,eMirrorClampToEdge,eClampToBorder};
enum class BorderColor {eFloatTransparentBlack,eFloatOpaqueBlack};
struct SamplerCreateInfo {
 Filter magFilter,minFilter;SamplerMipmapMode mipmapMode;SamplerAddressMode addressModeU,addressModeV,addressModeW;
 float mipLodBias,maxAnisotropy;int compareEnable;float minLod,maxLod;BorderColor borderColor;int unnormalizedCoordinates;bool anisotropyEnable;
};
using Sampler=int;
}
namespace texture {
vk::Filter translate_filter(SceGxmTextureFilter f){return f==SCE_GXM_TEXTURE_FILTER_POINT?vk::Filter::eNearest:vk::Filter::eLinear;}
using namespace renderer;
'''+ body('app/src/main/cpp/emucorev/ECV/ECV/renderer/src/vulkan/gxm_to_vulkan.cpp', 'vk::SamplerAddressMode translate_address_mode(') + r'''
}
struct SamplerCacheInfo {uint32_t value=0;int index=0;};
struct Queue {
 std::array<SamplerCacheInfo,8> items{};int next=0;
 Queue(){for(int i=0;i<8;i++)items[i].index=i;}
 SamplerCacheInfo* get_lru(){return &items[next++%8];}void set_as_mru(SamplerCacheInfo*){}
};
struct TextureCache {
 Queue sampler_queue;std::map<uint32_t,SamplerCacheInfo*> sampler_lookup;int last_bound_sampler_index=0;
 virtual void configure_sampler(size_t,const SceGxmTexture&,bool)=0;
 int cache_and_bind_sampler(const SceGxmTexture&,bool=false);
};
struct Device {
 std::map<int,vk::SamplerCreateInfo> infos;int next=1;
 int createSampler(vk::SamplerCreateInfo info){infos.emplace(next,info);return next++;}void destroy(int s){infos.erase(s);}
};
struct Features {bool use_texture_viewport=true;};
struct VKState {Device device;renderer::SamplerSupport sampler_support;Features features;
 VKState(){sampler_support.anisotropy=true;sampler_support.max_anisotropy=16;sampler_support.max_lod_bias=16;}};
struct VKTextureCache:TextureCache {
 VKState state;std::vector<vk::Sampler> samplers=std::vector<int>(8);int anisotropic_filtering=8;
 void configure_sampler(size_t,const SceGxmTexture&,bool)override;
 vk::SamplerCreateInfo info(int index){return state.device.infos.at(samplers[index]);}
};
'''
cpp += body('app/src/main/cpp/emucorev/ECV/ECV/renderer/src/texture/cache.cpp', 'int TextureCache::cache_and_bind_sampler(')
cpp += body('app/src/main/cpp/emucorev/ECV/ECV/renderer/src/vulkan/texture.cpp', 'void VKTextureCache::configure_sampler(')
cpp += r'''
int failures=0,checks=0;void check(bool b,const char*s){++checks;if(!b){++failures;printf("FAIL %s\n",s);}}
int main(){
 for(int strided: {0,SCE_GXM_TEXTURE_LINEAR_STRIDED}){
  VKTextureCache cache;SceGxmTexture color;color.type=strided;
  int colorIndex=cache.cache_and_bind_sampler(color);auto linear=cache.info(colorIndex);
  check(linear.magFilter==vk::Filter::eLinear&&linear.minFilter==vk::Filter::eLinear,"ordinary colors retain bilinear filtering");
  check(linear.mipmapMode==(strided?vk::SamplerMipmapMode::eNearest:vk::SamplerMipmapMode::eLinear)&&linear.anisotropyEnable,"ordinary colors retain supported filtering and anisotropy");
  for(auto format: {SCE_GXM_TEXTURE_BASE_FORMAT_F32,SCE_GXM_TEXTURE_BASE_FORMAT_F32M,SCE_GXM_TEXTURE_BASE_FORMAT_F32F32}){
   SceGxmTexture data=color;data.set_format(format);int pointIndex=cache.cache_and_bind_sampler(data);auto point=cache.info(pointIndex);
   check(pointIndex!=colorIndex,"unfilterable guest format cannot reuse a color sampler");
   check(point.magFilter==vk::Filter::eNearest&&point.minFilter==vk::Filter::eNearest,"32-bit float data uses exact texels");
   check(point.mipmapMode==vk::SamplerMipmapMode::eNearest,"32-bit float data does not interpolate packed mip payloads");
   check(!point.anisotropyEnable,"32-bit float data does not combine anisotropic samples");
   data.set_format(static_cast<uint32_t>(format)|0x6000u);
   check(cache.cache_and_bind_sampler(data)==pointIndex,"guest swizzle retains restricted format policy");
  }
  SceGxmTexture half=color;half.set_format(SCE_GXM_TEXTURE_BASE_FORMAT_F16F16F16F16);
  check(cache.cache_and_bind_sampler(half)==colorIndex,"filterable half floats reuse ordinary filtering");
  check(cache.cache_and_bind_sampler(color)==colorIndex,"float data cannot poison cached color sampling");
  int unsupported=cache.cache_and_bind_sampler(color,true);auto point=cache.info(unsupported);
  check(point.magFilter==vk::Filter::eNearest&&point.mipmapMode==vk::SamplerMipmapMode::eNearest&&!point.anisotropyEnable,"host filtering restriction also covers mip filtering");
  VKTextureCache reverse;SceGxmTexture data=color;data.set_format(SCE_GXM_TEXTURE_BASE_FORMAT_F32F32);
  int dataFirst=reverse.cache_and_bind_sampler(data);int colorSecond=reverse.cache_and_bind_sampler(color);
  check(dataFirst!=colorSecond&&reverse.info(colorSecond).magFilter==vk::Filter::eLinear,"nearest-first cache order cannot suppress color filtering");
  SceGxmTexture packed=color;packed.set_format(SCE_GXM_TEXTURE_BASE_FORMAT_U2U10U10U10);
  check(reverse.cache_and_bind_sampler(packed)==colorSecond,"filterable packed color stays linear");
 }
 for(bool reverse: {false,true}){
  VKTextureCache cache;SceGxmTexture a;a.type=SCE_GXM_TEXTURE_LINEAR_STRIDED;
  a.mip_filter=1;a.min_filter=0;a.lod_bias=5;a.lod_min0=3;a.lod_min1=2;
  SceGxmTexture b=a;b.mip_filter=0;b.min_filter=1;b.lod_bias=63;b.lod_min0=0;b.lod_min1=0;
  int first=cache.cache_and_bind_sampler(reverse?b:a);
  int second=cache.cache_and_bind_sampler(reverse?a:b);
  auto info=cache.info(first);
  check(first==second,"different strides share the same logical sampler");
  check(info.mipmapMode==vk::SamplerMipmapMode::eNearest,"stride bits do not enable mip interpolation");
  check(info.mipLodBias==0&&info.minLod==0,"stride bits do not become texture LOD");
  check(info.minFilter==info.magFilter,"strided minification uses the guest mag filter");
 }
 // P6: address modes, border colour, mirror clamp, LOD limits, unnormalized coordinates, anisotropy
 {
  VKTextureCache c;SceGxmTexture t;
  t.uaddr_mode=SCE_GXM_TEXTURE_ADDR_CLAMP_FULL_BORDER;t.vaddr_mode=SCE_GXM_TEXTURE_ADDR_MIRROR;
  auto i=c.info(c.cache_and_bind_sampler(t));
  check(i.addressModeU==vk::SamplerAddressMode::eClampToBorder&&i.addressModeV==vk::SamplerAddressMode::eMirroredRepeat,"border and mirror address modes");
  check(i.borderColor==vk::BorderColor::eFloatTransparentBlack,"border colour is transparent black");
  t.uaddr_mode=SCE_GXM_TEXTURE_ADDR_CLAMP_IGNORE_BORDER;t.vaddr_mode=SCE_GXM_TEXTURE_ADDR_REPEAT_IGNORE_BORDER;
  i=c.info(c.cache_and_bind_sampler(t));
  check(i.addressModeU==vk::SamplerAddressMode::eClampToEdge&&i.addressModeV==vk::SamplerAddressMode::eRepeat,"ignore-border modes are plain clamp / repeat");
 }
 for(bool ext:{false,true}){
  VKTextureCache c;c.state.sampler_support.mirror_clamp_to_edge=ext;SceGxmTexture t;
  t.uaddr_mode=SCE_GXM_TEXTURE_ADDR_MIRROR_CLAMP;t.vaddr_mode=SCE_GXM_TEXTURE_ADDR_MIRROR_CLAMP;
  auto i=c.info(c.cache_and_bind_sampler(t));
  check(i.addressModeU==(ext?vk::SamplerAddressMode::eMirrorClampToEdge:vk::SamplerAddressMode::eClampToEdge),"mirror clamp follows the device capability");
 }
 {
  VKTextureCache c;c.state.sampler_support.max_lod_bias=2.0f;SceGxmTexture t;
  t.lod_bias=63;check(c.info(c.cache_and_bind_sampler(t)).mipLodBias==2.0f,"LOD bias above the device limit is clamped");
  t.lod_bias=0;check(c.info(c.cache_and_bind_sampler(t)).mipLodBias==-2.0f,"LOD bias below the device limit is clamped");
  t.lod_bias=39;check(c.info(c.cache_and_bind_sampler(t)).mipLodBias==1.0f,"LOD bias inside the limit is kept");
  t.lod_bias=31;t.lod_min0=1;t.lod_min1=2;auto i=c.info(c.cache_and_bind_sampler(t));
  check(i.minLod==6.0f&&i.maxLod==VK_LOD_CLAMP_NONE,"LOD limits");
 }
 {
  // normalize_mode == 0: texel unit coordinates; the shader scales them when it has the texture viewport
  VKTextureCache c;SceGxmTexture n;SceGxmTexture u;u.normalize_mode=0;u.mip_filter=1;u.lod_bias=39;u.lod_min0=1;
  int in=c.cache_and_bind_sampler(n),iu=c.cache_and_bind_sampler(u);
  auto i=c.info(iu);
  check(in!=iu,"unnormalized textures do not share the normalized sampler");
  check(!i.unnormalizedCoordinates&&i.minLod==0&&i.maxLod==0&&i.mipLodBias==0&&i.mipmapMode==vk::SamplerMipmapMode::eNearest,"unnormalized texture, coordinates scaled by the shader: no mips");
  c.state.features.use_texture_viewport=false;
  VKTextureCache d;d.state.features.use_texture_viewport=false;
  SceGxmTexture w=u;w.min_filter=0;w.mag_filter=1;w.uaddr_mode=SCE_GXM_TEXTURE_ADDR_REPEAT;w.vaddr_mode=SCE_GXM_TEXTURE_ADDR_MIRROR;
  auto k=d.info(d.cache_and_bind_sampler(w));
  check(k.unnormalizedCoordinates&&k.minLod==0&&k.maxLod==0,"without the texture viewport the sampler is unnormalized");
  check(k.minFilter==k.magFilter,"unnormalized sampler needs equal filters");
  check(k.addressModeU==vk::SamplerAddressMode::eClampToEdge&&k.addressModeV==vk::SamplerAddressMode::eClampToEdge,"unnormalized sampler only clamps");
  check(!k.anisotropyEnable,"unnormalized sampler has no anisotropy");
 }
 {
  VKTextureCache c;c.state.sampler_support.max_anisotropy=4.0f;SceGxmTexture t;
  auto i=c.info(c.cache_and_bind_sampler(t));
  check(i.anisotropyEnable&&i.maxAnisotropy==4.0f,"anisotropy is limited by the device");
  VKTextureCache d;d.state.sampler_support.anisotropy=false;
  check(!d.info(d.cache_and_bind_sampler(t)).anisotropyEnable,"no anisotropy without the device feature");
 }
 printf("%d checks, %d failures\n",checks,failures);return !!failures;
}
'''
(out / 'probe.cpp').write_text(cpp, encoding='utf-8')
if os.name == 'nt' and Path('C:/msys64/ucrt64/bin').is_dir():
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler
subprocess.run([compiler, '-std=c++20', '-O0', str(out / 'probe.cpp'), '-o', str(out / 'probe.exe')], check=True)
result = subprocess.run([str(out / 'probe.exe')], capture_output=True, text=True, timeout=10)
print(result.stdout + result.stderr)
(out / ('red.log' if result.returncode else 'green.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
raise SystemExit(result.returncode)
