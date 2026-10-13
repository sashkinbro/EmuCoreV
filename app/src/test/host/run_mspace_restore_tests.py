#!/usr/bin/env python3
"""Exercise legacy guest heap relocation with the actual bundled dlmalloc."""
from pathlib import Path
import os
import subprocess

root = Path(__file__).resolve().parents[4]
allocator = root / 'app/src/main/cpp/vita3k/external/dlmalloc'
out = root / 'app/build/mspace-restore-probe'
out.mkdir(parents=True, exist_ok=True)
available = 'mspace_prepare_external' in (allocator / 'dlmalloc.h').read_text()
cpp = r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <map>
#include <set>
#include <type_traits>
// Expose the real allocator metadata only to construct corrupt fixtures.
#define private public
#include <dlmalloc.cc>
#undef private
'''
if not available:
    cpp += 'int mspace_prepare_external(mspace, int (*)(void*, size_t, void*), void*) { return 1; }\n'
cpp += r'''
struct Region { unsigned char *base; size_t size; };
int valid(void *ptr, size_t size, void *context) {
    const auto &r = *static_cast<Region*>(context);
    const auto p = reinterpret_cast<uintptr_t>(ptr), b = reinterpret_cast<uintptr_t>(r.base);
    return p >= b && p - b <= r.size && size <= r.size - (p - b);
}
unsigned checks=0, failures=0;
void check(bool ok, const char *label) { ++checks; if (!ok) { ++failures; printf("FAIL %s\n", label); } }
int main() {
    constexpr size_t capacity=1<<20;
    auto *source=static_cast<unsigned char*>(std::malloc(capacity));
    auto *target=static_cast<unsigned char*>(std::malloc(capacity));
    const auto heap=create_mspace_with_base(source, capacity, 0);
    const auto handle=static_cast<unsigned char*>(heap)-source;
    std::vector<size_t> live, sizes;
    for (unsigned i=0; i<180; ++i) {
        size_t size=(i%6==0)?4096+(i%5)*512:16+(i%11)*16;
        auto *p=static_cast<unsigned char*>(mspace_malloc(heap,size));
        check(p!=nullptr,"initial allocation");
        if (!p) return 2;
        std::memset(p,static_cast<int>(i+1),size);
        live.push_back(p-source); sizes.push_back(size);
    }
    for (unsigned i=1; i<live.size(); i+=3) {
        mspace_free(heap,source+live[i]); live[i]=0;
    }
    auto *aligned=static_cast<unsigned char*>(mspace_memalign(heap,256,1000));
    check(aligned && reinterpret_cast<uintptr_t>(aligned)%256==0,"initial memalign");
    std::memcpy(target,source,capacity);
    const auto original_magic=mparams._magic;
    Region region{target,capacity};
    auto clone=static_cast<mspace>(target+handle);
    check(mspace_prepare_external(clone,valid,&region)==1,"relocated heap accepted");
    for (unsigned i=0;i<live.size();++i) if(live[i]) {
        bool unchanged=true; for(size_t j=0;j<sizes[i];++j) unchanged &= target[live[i]+j]==static_cast<unsigned char>(i+1);
        check(unchanged,"live allocation payload preserved");
    }
    for(unsigned i=0;i<90;++i) {
        auto *p=static_cast<unsigned char*>(mspace_malloc(clone,32+(i%9)*16));
        check(valid(p,32+(i%9)*16,&region),"allocation belongs to restored RAM");
        if (p) mspace_free(clone,p);
    }
    if (failures) { printf("%u checks, %u failures\n",checks,failures); return 1; }
    auto *grown=static_cast<unsigned char*>(mspace_realloc(clone,target+live[0],9000));
    check(valid(grown,9000,&region),"realloc belongs to restored RAM");
    auto *a=static_cast<unsigned char*>(mspace_memalign(clone,512,800));
    check(valid(a,800,&region)&&reinterpret_cast<uintptr_t>(a)%512==0,"restored memalign");
    mspace_free(clone,a);
    check(mspace_prepare_external(clone,valid,&region)==1,"preparation is idempotent");
    // A new process has a fresh allocator magic even if guest RAM lands at the same address.
    mparams._magic ^= 0x123450;
    check(mspace_prepare_external(clone,valid,&region)==1,"fresh process magic accepted");
    auto *cold=static_cast<unsigned char*>(mspace_calloc(clone,12,20));
    check(valid(cold,240,&region),"cold restored calloc");
    if(cold) {bool zero=true;for(unsigned i=0;i<240;++i)zero &= cold[i]==0;check(zero,"calloc remains zero initialized");mspace_free(clone,cold);}
    check(destroy_mspace(clone)==0,"external heap destroy does not free guest RAM");
    // Missing mapped bytes must be rejected before touching the snapshot.
    std::memcpy(target,source,capacity);
    std::vector<unsigned char> before(target,target+capacity);
    region.size=handle+sizeof(malloc_state);
    check(mspace_prepare_external(clone,valid,&region)==0,"truncated heap rejected");
    check(std::memcmp(before.data(),target,capacity)==0,"rejection leaves heap unchanged");
    region.size=capacity;
    std::memcpy(target,source,capacity);
    auto *state=static_cast<mstate>(clone);
    state->_dv=reinterpret_cast<mchunkptr>(source+live[0]-2*sizeof(size_t));
    state->_dvsize=mem2chunk(source+live[0])->chunksize();
    before.assign(target,target+capacity);
    check(mspace_prepare_external(clone,valid,&region)==0,"dv cannot consume a live allocation");
    check(std::memcmp(before.data(),target,capacity)==0,"invalid dv rejected without mutation");
    std::memcpy(target,source,capacity);
    mem2chunk(clone)->_head ^= CINUSE_BIT;
    before.assign(target,target+capacity);
    check(mspace_prepare_external(clone,valid,&region)==0,"metadata chunk must remain allocated");
    check(std::memcmp(before.data(),target,capacity)==0,"invalid metadata rejected without mutation");
    std::memcpy(target,source,capacity);
    for(unsigned i=2;i<32;++i) {
        auto *header=state->smallbin_at(i);
        const auto sentinel=reinterpret_cast<uintptr_t>(source+handle)
            +(reinterpret_cast<uintptr_t>(header)-reinterpret_cast<uintptr_t>(state));
        if(!(state->_smallmap&(1u<<i)) && reinterpret_cast<uintptr_t>(header->_fd)==sentinel) {
            state->_smallmap |= 1u<<i;
            before.assign(target,target+capacity);
            check(mspace_prepare_external(clone,valid,&region)==0,"marked empty small bin rejected");
            check(std::memcmp(before.data(),target,capacity)==0,"invalid bin rejected without mutation");
            break;
        }
    }
    Region same_base{source,handle+sizeof(malloc_state)};
    mparams._magic=original_magic;
    before.assign(source,source+capacity);
    check(mspace_prepare_external(heap,valid,&same_base)==0,"same-base truncated heap rejected");
    check(std::memcmp(before.data(),source,capacity)==0,"same-base rejection leaves heap unchanged");
    mparams._magic=original_magic;
    destroy_mspace(heap);std::free(source);std::free(target);
    printf("mspace restore: %u checks, %u failures\n",checks,failures);return !!failures;
}
'''
(out/'probe.cpp').write_text(cpp)
compiler = Path('C:/msys64/ucrt64/bin/g++.exe')
env = os.environ.copy()
env['PATH'] = str(compiler.parent) + ';' + env.get('PATH', '')
subprocess.run([str(compiler),'-std=c++20','-O0','-DONLY_MSPACES=1','-DUSE_LOCK=0',
               '-I'+str(allocator),str(out/'probe.cpp'),'-o',str(out/'probe.exe')],check=True,env=env)
run = subprocess.run([str(out/'probe.exe')],capture_output=True,text=True,env=env,timeout=20)
print(run.stdout+run.stderr)
(out/('green.log' if run.returncode==0 else 'red.log')).write_text(run.stdout+run.stderr)
raise SystemExit(run.returncode)
