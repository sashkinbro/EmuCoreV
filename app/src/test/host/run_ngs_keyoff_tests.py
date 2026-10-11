#!/usr/bin/env python3
"""Compile actual NGS scheduler/envelope plus exact KeyOff export body.
Only guest callback execution and unused routing/kernel services are stubbed.
"""
from pathlib import Path
import os, shutil, subprocess
root=Path(__file__).resolve().parents[4]
core=root/'app/src/main/cpp/vita3k/vita3k'; ext=core.parent/'external'
out=root/'app/build/ngs-keyoff-probe';out.mkdir(parents=True,exist_ok=True)
if os.name=='nt':os.environ['PATH']='C:/msys64/ucrt64/bin;'+os.environ['PATH']
def block(path,marker):
 s=path.read_text();assert s.count(marker)==1,marker
 start=s.index(marker);brace=s.index('{',start);depth=0
 for end in range(brace,len(s)):
  depth+=(s[end]=='{')-(s[end]=='}')
  if not depth:return s[start:end+1]
 raise AssertionError(marker)
stubs=out/'stubs/kernel';stubs.mkdir(parents=True,exist_ok=True)
(stubs/'state.h').write_text('''#pragma once
#include <memory>
#include <initializer_list>
#include <cstdint>
#include <cstdlib>
struct TestThread {void run_callback(uint32_t,std::initializer_list<uint32_t>){std::abort();}};
struct KernelState {std::shared_ptr<TestThread> get_thread(int){std::abort();}};
''')
ngs=core/'ngs/src/ngs.cpp'
errors=block(core/'modules/SceNgsUser/SceNgs.cpp','enum SceNgsErrorCode : uint32_t')+';'
constructors='\n'.join(block(ngs,m) for m in ('Rack::Rack(', 'System::System(', 'ModuleData::ModuleData()', 'bool Patch::is_active()', 'void VoiceInputManager::reset_inputs()', 'void Voice::transition('))
keyoff=block(core/'modules/SceNgsUser/SceNgs.cpp','EXPORT(SceInt32, sceNgsVoiceKeyOff,').replace('EXPORT(SceInt32, sceNgsVoiceKeyOff, ngs::Voice *voice)', 'SceInt32 keyoff(TestEnv &emuenv, SceUID thread_id, ngs::Voice *voice)')
probe=r'''
#include <gtest/gtest.h>
#include <ngs/modules/envelope.h>
''' + errors + r'''
#include <kernel/state.h>
#include <atomic>
#include <future>
#include <chrono>
#define TRACY_FUNC(...)
#define RET_ERROR(value) (value)
struct TestEnv {struct {struct {bool ngs_enable=true;} current_config;} cfg;KernelState kernel;MemState mem;};
struct CallbackRecord {std::atomic<bool> lock=false;int calls=0;bool reacquired_held_lock=false;int thread=0;uint32_t module=0;};
CallbackRecord *record=nullptr;
Address host_to_guest(const MemState &,const void *){std::abort();}
namespace ngs {
''' + constructors + r'''
// Guest execution seam: emulate attempting the game's nonrecursive spinlock,
// report the reentry instead of hanging the baseline regression indefinitely.
void Voice::invoke_callback(KernelState &,const MemState &,const SceUID thread,Ptr<void>,Ptr<void>,const uint32_t module,const uint32_t,const uint32_t,Address){
 record->calls++;record->thread=thread;record->module=module;
 if(record->lock.exchange(true))record->reacquired_held_lock=true;
 else record->lock=false;
}
bool deliver_data(const MemState &,const std::vector<Voice *> &,Voice *,const uint8_t,const VoiceProduct &){std::abort();}
bool deliver_data_to_master(const MemState &,Voice *,Voice *,const VoiceProduct &){std::abort();}
void release_rack(State &,const MemState &,System *,Rack *){std::abort();}
Ptr<Patch> Voice::patch(const MemState &,const int32_t,int32_t,int32_t,Ptr<Voice>,Ptr<Voice>){std::abort();}
}
''' + keyoff + r'''
struct NgsKeyOffFixture:testing::Test {
 TestEnv env;ngs::System system{Ptr<void>(0),0};ngs::Rack rack{&system,Ptr<void>(0),0};ngs::VoiceDefinition definition{};ngs::Voice voice{};CallbackRecord callbacks;
 void SetUp()override{
  record=&callbacks;env.mem.memory=Memory(new uint8_t[4096]{},[](uint8_t*p){delete[]p;});
  system.granularity=480;system.sample_rate=48000;rack.vdef=&definition;definition.output_count=0;
  rack.modules.push_back(std::make_unique<ngs::EnvelopeModule>());
  voice.rack=&rack;voice.state=ngs::VOICE_STATE_ACTIVE;voice.voice_mutex=std::make_unique<std::mutex>();voice.finished_callback=Ptr<void>(0x101);
  voice.datas.resize(1);auto &data=voice.datas[0];data.parent=&voice;data.index=0;data.info={Ptr<void>(0x100),sizeof(SceNgsEnvelopeParams)};
  auto *params=data.get_parameters<SceNgsEnvelopeParams>(env.mem);*params={};params->desc.id=SCE_NGS_ENVELOPE_PARAMS_STRUCT_ID;params->uNumPoints=1;params->envelopePoints[0].fAmplitude=1;params->nLoopEnd=-1;params->uReleaseMsecs=20;
  system.voice_scheduler.queue.push_back(&voice);
 }
 void update(){system.voice_scheduler.update(env.kernel,env.mem,108);}
 void key_off_while_caller_owns_lock(){callbacks.lock=true;EXPECT_EQ(keyoff(env,109,&voice),0);callbacks.lock=false;}
};
TEST_F(NgsKeyOffFixture, CallerLockIsReleasedBeforeFinishedCallbackAndReleaseCompletesExactlyOnce){
 key_off_while_caller_owns_lock();EXPECT_EQ(callbacks.calls,0);EXPECT_FALSE(callbacks.reacquired_held_lock);
 EXPECT_EQ(voice.state,ngs::VOICE_STATE_FINALIZING);EXPECT_EQ(system.voice_scheduler.queue.size(),1u);
 update();EXPECT_EQ(callbacks.calls,0);EXPECT_EQ(voice.state,ngs::VOICE_STATE_FINALIZING);
 update();EXPECT_EQ(callbacks.calls,1);EXPECT_EQ(callbacks.thread,108);EXPECT_EQ(callbacks.module,0x5CE3u);EXPECT_FALSE(callbacks.reacquired_held_lock);EXPECT_EQ(voice.state,ngs::VOICE_STATE_AVAILABLE);
 update();EXPECT_EQ(callbacks.calls,1);EXPECT_TRUE(system.voice_scheduler.queue.empty());
}
TEST_F(NgsKeyOffFixture, PausedVoiceRetainsReleaseUntilResume){
 ASSERT_TRUE(system.voice_scheduler.pause(env.mem,&voice));key_off_while_caller_owns_lock();EXPECT_EQ(callbacks.calls,0);EXPECT_TRUE(voice.is_paused);EXPECT_EQ(voice.state,ngs::VOICE_STATE_FINALIZING);
 update();EXPECT_EQ(callbacks.calls,0);ASSERT_TRUE(system.voice_scheduler.resume(env.mem,&voice));update();EXPECT_EQ(callbacks.calls,0);update();EXPECT_EQ(callbacks.calls,1);
}
TEST_F(NgsKeyOffFixture, AlreadyAvailableVoiceDoesNotEmitFinishedCallback){
 system.voice_scheduler.stop(env.mem,&voice);key_off_while_caller_owns_lock();update();EXPECT_EQ(callbacks.calls,0);EXPECT_FALSE(callbacks.reacquired_held_lock);EXPECT_EQ(voice.state,ngs::VOICE_STATE_AVAILABLE);
}
TEST_F(NgsKeyOffFixture, RepeatedKeyOffDoesNotDuplicateCompletion){
 key_off_while_caller_owns_lock();key_off_while_caller_owns_lock();EXPECT_EQ(callbacks.calls,0);update();update();update();EXPECT_EQ(callbacks.calls,1);
}
TEST_F(NgsKeyOffFixture, KeyOffWaitsForSchedulerUpdateLock){
 std::unique_lock scheduler_lock(system.voice_scheduler.mutex);
 auto pending=std::async(std::launch::async,[&]{return keyoff(env,109,&voice);});
 const auto before=pending.wait_for(std::chrono::milliseconds(20));
 scheduler_lock.unlock();EXPECT_EQ(pending.get(),0);
 EXPECT_EQ(before,std::future_status::timeout);EXPECT_EQ(callbacks.calls,0);
 EXPECT_EQ(voice.state,ngs::VOICE_STATE_FINALIZING);
}
TEST_F(NgsKeyOffFixture, KeyOffWaitsForVoiceStateLock){
 std::unique_lock voice_lock(*voice.voice_mutex);
 auto pending=std::async(std::launch::async,[&]{return keyoff(env,109,&voice);});
 const auto before=pending.wait_for(std::chrono::milliseconds(20));
 voice_lock.unlock();EXPECT_EQ(pending.get(),0);
 EXPECT_EQ(before,std::future_status::timeout);EXPECT_EQ(callbacks.calls,0);
 EXPECT_EQ(voice.state,ngs::VOICE_STATE_FINALIZING);
}
'''
(out/'probe.cpp').write_text(probe)
includes=[out/'stubs']+[core/(p+'/include') for p in ('ngs','mem','util','codec')]+[ext/'fmt/include',ext/'spdlog/include',ext/'boost',ext/'googletest/googletest/include',ext/'googletest/googletest']
compiler=os.environ.get('CXX') or shutil.which('g++');assert compiler
cmd=[compiler,'-std=c++23','-O0','-g0','-pthread','-ffunction-sections','-fdata-sections','-DFMT_HEADER_ONLY','-DSPDLOG_FMT_EXTERNAL','-DBOOST_ALL_NO_LIB']+['-I'+str(p) for p in includes]
cmd += [str(out/'probe.cpp'),str(core/'ngs/src/scheduler.cpp'),str(core/'ngs/src/modules/envelope.cpp'),str(ext/'googletest/googletest/src/gtest-all.cc'),str(ext/'googletest/googletest/src/gtest_main.cc'),'-Wl,--gc-sections','-o',str(out/'probe.exe')]
subprocess.run(cmd,check=True)
run=subprocess.run([str(out/'probe.exe')],capture_output=True,text=True,timeout=15)
print(run.stdout+run.stderr);(out/('red.log' if run.returncode else 'green.log')).write_text(run.stdout+run.stderr)
raise SystemExit(run.returncode)
