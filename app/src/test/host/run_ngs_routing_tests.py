#!/usr/bin/env python3
"""Exercise actual scheduler, routing and input mixing with deterministic PCM.

Only PCM generation, guest callbacks and unused rack allocation are seams.
"""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
core = root / 'app/src/main/cpp/vita3k/vita3k'
ext = core.parent / 'external'
out = root / 'app/build/ngs-routing-probe'
out.mkdir(parents=True, exist_ok=True)
if os.name == 'nt':
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']


def block(path, marker):
    source = path.read_text()
    assert source.count(marker) == 1, marker
    start = source.index(marker)
    brace = source.index('{', start)
    depth = 0
    for end in range(brace, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if not depth:
            return source[start:end + 1]
    raise AssertionError(marker)


stubs = out / 'stubs/kernel'
stubs.mkdir(parents=True, exist_ok=True)
(stubs / 'state.h').write_text('''#pragma once
#include <memory>
#include <initializer_list>
#include <cstdint>
#include <cstdlib>
struct TestThread {void run_callback(uint32_t,std::initializer_list<uint32_t>){std::abort();}};
struct KernelState {std::shared_ptr<TestThread> get_thread(int){std::abort();}};
''')
ngs = core / 'ngs/src/ngs.cpp'
bodies = '\n'.join(block(ngs, marker) for marker in (
    'Rack::Rack(', 'System::System(', 'ModuleData::ModuleData()',
    'bool Patch::is_active()', 'void VoiceInputManager::init(',
    'void VoiceInputManager::reset_inputs()',
    'VoiceInputManager::PCMInput *VoiceInputManager::get_input_buffer_queue(',
    'int32_t VoiceInputManager::receive(', 'void Voice::transition('))
probe = r'''
#include <gtest/gtest.h>
#include <ngs/system.h>
#include <ngs/modules/envelope.h>
#include <kernel/state.h>
#include <new>
Address host_to_guest(const MemState &mem,const void *ptr){return static_cast<const uint8_t*>(ptr)-mem.memory.get();}
namespace ngs {
''' + bodies + r'''
void Voice::invoke_callback(KernelState &,const MemState &,SceUID,Ptr<void>,Ptr<void>,uint32_t,uint32_t,uint32_t,Address){std::abort();}
void release_rack(State &,const MemState &,System *,Rack *){std::abort();}
Ptr<Patch> Voice::patch(const MemState &,int32_t,int32_t,int32_t,Ptr<Voice>,Ptr<Voice>){std::abort();}
}
// Known stereo PCM or passthrough of actual received input; routing stays real.
struct SignalModule:ngs::Module {
 bool pass;std::vector<float> pcm=std::vector<float>(16);
 explicit SignalModule(bool pass=false):pass(pass){}
 uint32_t get_buffer_parameter_size()const override{return 0;}
 uint32_t module_id()const override{return 0x5CAA;}
 bool process(KernelState&,const MemState&,SceUID,ngs::ModuleData &d,std::unique_lock<std::recursive_mutex>&,std::unique_lock<std::mutex>&)override{
  if(pass)std::copy_n(reinterpret_cast<const float*>(d.parent->inputs.inputs[0].data()),16,pcm.begin());
  else for(int i=0;i<8;++i){pcm[i*2]=0.25f;pcm[i*2+1]=-0.125f;}
  d.parent->products[0].data=reinterpret_cast<uint8_t*>(pcm.data());return false;
 }
};
struct Routing:testing::Test {
 MemState mem;KernelState kern;ngs::System system{Ptr<void>(0),0};
 ngs::VoiceDefinition source_def{},master_def{},mixer_def{};
 ngs::Rack source_rack{&system,Ptr<void>(0),0},master_rack{&system,Ptr<void>(0),0},mixer_rack{&system,Ptr<void>(0),0};
 std::vector<ngs::Voice*> allocated;Address cursor=0x1000;ngs::Voice *master,*a,*b;
 ngs::Voice *voice(ngs::Rack &rack){
  cursor=(cursor+alignof(ngs::Voice)-1)&~(alignof(ngs::Voice)-1);
  auto*v=new(mem.memory.get()+cursor)ngs::Voice{};cursor+=sizeof(ngs::Voice);
  v->rack=&rack;v->voice_mutex=std::make_unique<std::mutex>();v->state=ngs::VOICE_STATE_ACTIVE;v->inputs.init(8,1);
  v->datas.resize(rack.modules.size());for(auto&d:v->datas)d.parent=v;
  rack.voices.emplace_back(v,mem);allocated.push_back(v);system.voice_scheduler.queue.push_back(v);return v;
 }
 ngs::Patch *patch(ngs::Voice *src,ngs::Voice *dst,float gain=1,bool removed=false){
  cursor=(cursor+alignof(ngs::Patch)-1)&~(alignof(ngs::Patch)-1);
  auto*p=new(mem.memory.get()+cursor)ngs::Patch{};cursor+=sizeof(ngs::Patch);
  p->source=Ptr<ngs::Voice>(src,mem);p->dest=Ptr<ngs::Voice>(dst,mem);p->output_sub_index=removed?-1:0;
  p->volume_matrix[0][0]=p->volume_matrix[1][1]=gain;src->patches[0].emplace_back(p,mem);return p;
 }
 void SetUp()override{
  mem.memory=Memory(new uint8_t[128*1024]{},[](uint8_t*p){delete[]p;});system.granularity=8;system.sample_rate=48000;
  source_def.type=ngs::BussType::BUSS_ATRAC9;source_def.output_count=1;master_def.type=ngs::BussType::BUSS_MASTER;master_def.output_count=0;mixer_def.type=ngs::BussType::BUSS_MIXER;mixer_def.output_count=1;
  source_rack.vdef=&source_def;master_rack.vdef=&master_def;mixer_rack.vdef=&mixer_def;
  for(auto*r:{&source_rack,&master_rack,&mixer_rack}){r->channels_per_voice=2;system.racks.push_back(r);}
  source_rack.modules.push_back(std::make_unique<SignalModule>());mixer_rack.modules.push_back(std::make_unique<SignalModule>(true));
  master=voice(master_rack);a=voice(source_rack);b=voice(source_rack);
 }
 void TearDown()override{for(auto*v:allocated)v->~Voice();}
 void update(){system.voice_scheduler.update(kern,mem,108);}
 void expect(ngs::Voice*v,float left,float right){const auto*pcm=reinterpret_cast<const float*>(v->inputs.inputs[0].data());for(int i=0;i<8;++i){EXPECT_NEAR(pcm[i*2],left,0.000001f);EXPECT_NEAR(pcm[i*2+1],right,0.000001f);}}
};
TEST_F(Routing, UnpatchedSourcesStaySilent){update();expect(master,0,0);}
TEST_F(Routing, GainCrossfeedAndMuteAreAppliedOnEveryUpdate){
 auto*p=patch(a,master);p->volume_matrix[0][0]=0.5f;p->volume_matrix[1][1]=0.25f;
 p->volume_matrix[0][1]=0.2f;p->volume_matrix[1][0]=0.4f;
 patch(b,master,0);system.voice_scheduler.queue={a,b,master};update();expect(master,0.075f,0.01875f);
 std::memset(p->volume_matrix,0,sizeof(p->volume_matrix));update();expect(master,0,0);
}
TEST_F(Routing, ExplicitRoutesCarryMoreThanEightSources){patch(a,master,0.1f);patch(b,master,0.1f);for(int i=0;i<10;++i)patch(voice(source_rack),master,0.1f);system.voice_scheduler.deque_voice(master);system.voice_scheduler.queue.push_back(master);update();expect(master,0.3f,-0.15f);}
TEST_F(Routing, RemovedPatchDoesNotResurrectEitherSource){patch(a,master,1,true);update();expect(master,0,0);}
TEST_F(Routing, OffQueuePatchHistoryBlocksFallback){patch(a,master,1,true);ASSERT_TRUE(system.voice_scheduler.pause(mem,a));update();expect(master,0,0);}
TEST_F(Routing, ZeroGainExplicitPatchMutesAndBlocksOtherFallback){patch(a,master,0);system.voice_scheduler.queue={a,b,master};update();expect(master,0,0);}
TEST_F(Routing, ExplicitMasterReceivesOnlyRoutedVoice){patch(a,master,0.5f);system.voice_scheduler.queue={a,b,master};update();expect(master,0.125f,-0.0625f);}
TEST_F(Routing, MultipleMastersDisableImplicitDelivery){auto*other=voice(master_rack);update();expect(master,0,0);expect(other,0,0);}
TEST_F(Routing, MissingMasterDoesNotCreateDestination){system.voice_scheduler.deque_voice(master);update();expect(master,0,0);}
TEST_F(Routing, UnfedMixerIsNotMadeAudible){source_def.type=ngs::BussType::BUSS_MIXER;update();expect(master,0,0);}
TEST_F(Routing, LegacyFedSubmixFallbackStillWorks){auto*mixer=voice(mixer_rack);patch(a,mixer,0.5f);system.voice_scheduler.queue={a,b,mixer,master};update();expect(master,0.125f,-0.0625f);}
TEST_F(Routing, LegacyFedSubmixLimitRemainsEight){system.voice_scheduler.queue.clear();for(int i=0;i<9;++i){auto*src=voice(source_rack);auto*mix=voice(mixer_rack);patch(src,mix,0.1f);}system.voice_scheduler.queue.push_back(master);update();expect(master,0.2f,-0.1f);}
TEST_F(Routing, PausedAndStoppedSourcesStaySilentEvenInStaleQueueCopy){patch(a,master);patch(b,master);a->is_paused=true;b->state=ngs::VOICE_STATE_AVAILABLE;system.voice_scheduler.queue={a,b,master};update();expect(master,0,0);EXPECT_EQ(a->frame_count,0u);EXPECT_EQ(b->frame_count,0u);}
TEST_F(Routing, PauseResumeStopDoNotReplayStalePcm){patch(a,master);system.voice_scheduler.queue={a,b,master};ASSERT_TRUE(system.voice_scheduler.pause(mem,a));ASSERT_TRUE(system.voice_scheduler.stop(mem,b));update();expect(master,0,0);ASSERT_TRUE(system.voice_scheduler.resume(mem,a));update();expect(master,0.25f,-0.125f);ASSERT_TRUE(system.voice_scheduler.stop(mem,a));update();expect(master,0,0);}
TEST_F(Routing, SourceThroughMixerHonorsBothRouteGains){auto*mixer=voice(mixer_rack);patch(a,mixer,0.5f);patch(mixer,master,0.4f);system.voice_scheduler.queue={a,b,mixer,master};update();expect(master,0.05f,-0.025f);}
TEST_F(Routing, MutedIntermediateMixerDoesNotFallBackToMaster){auto*mixer=voice(mixer_rack);patch(a,mixer,1);patch(mixer,master,0);system.voice_scheduler.queue={a,b,mixer,master};update();expect(master,0,0);}
'''
(out / 'probe.cpp').write_text(probe)
scheduler = core / 'ngs/src/scheduler.cpp'
includes = [out / 'stubs'] + [core / (p + '/include') for p in ('ngs', 'mem', 'util', 'codec')] + [ext / 'fmt/include', ext / 'spdlog/include', ext / 'boost', ext / 'googletest/googletest/include', ext / 'googletest/googletest']
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler
exe = out / 'probe.exe'
cmd = [compiler, '-std=c++23', '-O0', '-g0', '-pthread', '-ffunction-sections', '-fdata-sections', '-DFMT_HEADER_ONLY', '-DSPDLOG_FMT_EXTERNAL', '-DBOOST_ALL_NO_LIB'] + ['-I' + str(p) for p in includes]
cmd += [str(out / 'probe.cpp'), str(scheduler), str(core / 'ngs/src/route.cpp'), str(core / 'ngs/src/modules/envelope.cpp'), str(ext / 'googletest/googletest/src/gtest-all.cc'), str(ext / 'googletest/googletest/src/gtest_main.cc'), '-Wl,--gc-sections', '-o', str(exe)]
subprocess.run(cmd, check=True)
result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15)
print(result.stdout + result.stderr)
(out / 'green.log').write_text(result.stdout + result.stderr)
raise SystemExit(result.returncode)
