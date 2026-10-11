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
source_state_changes='\n'.join(block(core/('ngs/src/modules/'+name+'.cpp'), 'void '+cls+'::on_state_change(').replace('void '+cls+'::on_state_change', 'void '+name+'_state_change') for name,cls in [('player','PlayerModule'),('atrac9','Atrac9Module')])
keyoff=block(core/'modules/SceNgsUser/SceNgs.cpp','EXPORT(SceInt32, sceNgsVoiceKeyOff,').replace('EXPORT(SceInt32, sceNgsVoiceKeyOff, ngs::Voice *voice)', 'SceInt32 keyoff(TestEnv &emuenv, SceUID thread_id, ngs::Voice *voice)')
probe=r'''
#include <gtest/gtest.h>
#include <ngs/modules/envelope.h>
#include <ngs/modules/player.h>
#include <ngs/modules/atrac9.h>
#include <util/log.h>
''' + errors + r'''
#include <kernel/state.h>
#include <atomic>
#include <future>
#include <chrono>
#include <functional>
#define TRACY_FUNC(...)
#undef RET_ERROR
#define RET_ERROR(value) (value)
struct TestEnv {struct {struct {bool ngs_enable=true;} current_config;} cfg;KernelState kernel;MemState mem;};
struct CallbackRecord {std::atomic<bool> lock=false;int calls=0;bool reacquired_held_lock=false;int thread=0;uint32_t module=0;std::function<void()> on_callback;};
CallbackRecord *record=nullptr;
Address host_to_guest(const MemState &,const void *){std::abort();}
namespace ngs {
''' + constructors + '\n' + source_state_changes + r'''
// Guest execution seam: emulate attempting the game's nonrecursive spinlock,
// report the reentry instead of hanging the baseline regression indefinitely.
void Voice::invoke_callback(KernelState &,const MemState &,const SceUID thread,Ptr<void>,Ptr<void>,const uint32_t module,const uint32_t,const uint32_t,Address){
 record->calls++;record->thread=thread;record->module=module;
 if(record->lock.exchange(true))record->reacquired_held_lock=true;
 else record->lock=false;
 if(record->on_callback){auto action=std::move(record->on_callback);action();}
}
bool deliver_data(const MemState &,const std::vector<Voice *> &,Voice *,const uint8_t,const VoiceProduct &){std::abort();}
bool deliver_data_to_master(const MemState &,Voice *,Voice *,const VoiceProduct &){std::abort();}
void release_rack(State &,const MemState &,System *,Rack *){std::abort();}
Ptr<Patch> Voice::patch(const MemState &,const int32_t,int32_t,int32_t,Ptr<Voice>,Ptr<Voice>){std::abort();}
}
''' + keyoff + r'''

// The source never reaches end-of-data: model a genuine loop_count=-1 source,
// while executing the production Player/AT9 transition bodies verbatim.
struct LoopingSource:ngs::Module {
 bool atrac;int process_calls=0;bool emit_audio=false;std::vector<float> signal=std::vector<float>(960);
 explicit LoopingSource(bool atrac):atrac(atrac){}
 uint32_t get_buffer_parameter_size()const override{return atrac?sizeof(SceNgsAT9Params):sizeof(SceNgsPlayerParams);}
 uint32_t module_id()const override{return atrac?0x5CAA:0x5CE6;}
 bool process(KernelState&,const MemState&,SceUID,ngs::ModuleData&data,std::unique_lock<std::recursive_mutex>&,std::unique_lock<std::mutex>&)override{++process_calls;if(emit_audio){std::fill(signal.begin(),signal.end(),1.0f);data.parent->products[0].data=reinterpret_cast<uint8_t*>(signal.data());}return false;}
 void on_state_change(const MemState&mem,ngs::ModuleData&data,ngs::VoiceState previous)override{
  if(atrac)ngs::atrac9_state_change(mem,data,previous);else ngs::player_state_change(mem,data,previous);
 }
};
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

 LoopingSource *add_loop(bool atrac){
  auto source=std::make_unique<LoopingSource>(atrac);auto *result=source.get();rack.modules.insert(rack.modules.begin(),std::move(source));
  voice.datas.insert(voice.datas.begin(),ngs::ModuleData{});auto &d=voice.datas[0];d.parent=&voice;d.index=0;voice.datas[1].index=1;
  d.info={Ptr<void>(0x400),static_cast<uint32_t>(atrac?sizeof(SceNgsAT9Params):sizeof(SceNgsPlayerParams))};
  if(atrac){auto*p=d.get_parameters<SceNgsAT9Params>(env.mem);new(p)SceNgsAT9Params{};p->buffer_params[2].loop_count=-1;auto*st=d.get_state<SceNgsAT9States>();st->current_buffer=2;st->current_byte_position_in_buffer=700;d.get_logical_state<ngs::Atrac9LogicalState>()->current_loop_count=5;}
  else{auto*p=d.get_parameters<SceNgsPlayerParams>(env.mem);new(p)SceNgsPlayerParams{};p->start_buffer=0;p->start_bytes=7;p->buffer_params[2].loop_count=-1;auto*st=d.get_state<SceNgsPlayerStates>();st->current_buffer=2;st->current_byte_position_in_buffer=700;d.get_logical_state<ngs::PlayerLogicalState>()->current_loop_count=5;}
  return result;
 }
 void expect_position(bool atrac){auto &d=voice.datas[0];if(atrac){auto*st=d.get_state<SceNgsAT9States>();EXPECT_EQ(st->current_buffer,2);EXPECT_EQ(st->current_byte_position_in_buffer,700);EXPECT_EQ(d.get_logical_state<ngs::Atrac9LogicalState>()->current_loop_count,5);}else{auto*st=d.get_state<SceNgsPlayerStates>();EXPECT_EQ(st->current_buffer,2);EXPECT_EQ(st->current_byte_position_in_buffer,700);EXPECT_EQ(d.get_logical_state<ngs::PlayerLogicalState>()->current_loop_count,5);}}
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

TEST_F(NgsKeyOffFixture, LoopingPlayerWithoutEffectiveEnvelopeFinishesBeforeAnyReplay){
 auto*source=add_loop(false);voice.datas[1].is_bypassed=true;key_off_while_caller_owns_lock();EXPECT_EQ(callbacks.calls,0);update();EXPECT_EQ(source->process_calls,0);EXPECT_EQ(voice.state,ngs::VOICE_STATE_AVAILABLE);EXPECT_EQ(callbacks.calls,1);EXPECT_EQ(callbacks.module,0x5CE6u);EXPECT_FALSE(callbacks.reacquired_held_lock);update();EXPECT_EQ(callbacks.calls,1);
}
TEST_F(NgsKeyOffFixture, LoopingAtracWithoutEnvelopeFinishesOnceOnUpdater){
 auto*source=add_loop(true);rack.modules.pop_back();voice.datas.pop_back();key_off_while_caller_owns_lock();key_off_while_caller_owns_lock();update();EXPECT_EQ(source->process_calls,0);EXPECT_EQ(voice.state,ngs::VOICE_STATE_AVAILABLE);EXPECT_EQ(callbacks.calls,1);EXPECT_EQ(callbacks.module,0x5CAAu);update();EXPECT_EQ(callbacks.calls,1);
}
TEST_F(NgsKeyOffFixture, InvalidEmptyAndUninitializedEnvelopeCannotKeepLoopingPlayerAlive){
 for(int mode=0;mode!=3;++mode){callbacks.calls=0;voice.state=ngs::VOICE_STATE_ACTIVE;system.voice_scheduler.queue={&voice};if(rack.modules.size()==1)add_loop(false);auto&d=voice.datas[1];auto*p=d.get_parameters<SceNgsEnvelopeParams>(env.mem);p->desc.id=mode==0?0:SCE_NGS_ENVELOPE_PARAMS_STRUCT_ID;p->uNumPoints=mode==1?0:1;d.needs_reinit=mode==2;key_off_while_caller_owns_lock();update();EXPECT_EQ(voice.state,ngs::VOICE_STATE_AVAILABLE);EXPECT_EQ(callbacks.calls,1);}
}
TEST_F(NgsKeyOffFixture, PausedLoopWithoutEnvelopeFinishesOnlyAfterResume){
 add_loop(false);voice.datas[1].is_bypassed=true;ASSERT_TRUE(system.voice_scheduler.pause(env.mem,&voice));key_off_while_caller_owns_lock();update();EXPECT_EQ(callbacks.calls,0);ASSERT_TRUE(system.voice_scheduler.resume(env.mem,&voice));update();EXPECT_EQ(callbacks.calls,1);EXPECT_EQ(voice.state,ngs::VOICE_STATE_AVAILABLE);
}
TEST_F(NgsKeyOffFixture, PlayerEnvelopeReleaseKeepsCurrentPlaybackPosition){
 auto*source=add_loop(false);key_off_while_caller_owns_lock();expect_position(false);update();expect_position(false);EXPECT_EQ(voice.state,ngs::VOICE_STATE_FINALIZING);EXPECT_EQ(source->process_calls,1);EXPECT_EQ(callbacks.calls,0);update();EXPECT_EQ(voice.state,ngs::VOICE_STATE_AVAILABLE);EXPECT_EQ(callbacks.calls,1);EXPECT_EQ(callbacks.module,0x5CE3u);
}
TEST_F(NgsKeyOffFixture, AtracEnvelopeReleaseKeepsCurrentPlaybackPosition){
 add_loop(true);key_off_while_caller_owns_lock();expect_position(true);update();expect_position(true);EXPECT_EQ(callbacks.calls,0);update();EXPECT_EQ(callbacks.calls,1);EXPECT_EQ(voice.state,ngs::VOICE_STATE_AVAILABLE);
}
TEST_F(NgsKeyOffFixture, AudibleReleaseStartsAtCurrentGainAndFadesForTwentyMilliseconds){
 auto*source=add_loop(false);source->emit_audio=true;key_off_while_caller_owns_lock();update();
 EXPECT_FLOAT_EQ(source->signal[0],1.0f);EXPECT_NEAR(source->signal[958],0.5f+0.5f/480,0.0001f);
 auto*logical=voice.datas[1].get_logical_state<ngs::EnvelopeLogicalState>();
 EXPECT_FLOAT_EQ(logical->release_start_height,1.0f);EXPECT_FLOAT_EQ(logical->release_height,0.5f);EXPECT_EQ(callbacks.calls,0);
 update();EXPECT_FLOAT_EQ(source->signal[0],0.5f);EXPECT_NEAR(source->signal[958],0.5f/480,0.0001f);EXPECT_EQ(callbacks.calls,1);EXPECT_EQ(voice.state,ngs::VOICE_STATE_AVAILABLE);
}
TEST_F(NgsKeyOffFixture, FinishedCallbackPausesOtherFinalizingVoiceInSameUpdate){
 auto*source=add_loop(false);voice.datas[1].is_bypassed=true;
 ngs::Voice other{};other.rack=&rack;other.state=ngs::VOICE_STATE_ACTIVE;other.voice_mutex=std::make_unique<std::mutex>();other.finished_callback=Ptr<void>(0x101);other.datas.resize(2);
 for(int i=0;i<2;i++){other.datas[i].parent=&other;other.datas[i].index=i;other.datas[i].info=voice.datas[i].info;}other.datas[1].is_bypassed=true;
 system.voice_scheduler.queue.push_back(&other);EXPECT_EQ(keyoff(env,109,&other),0);key_off_while_caller_owns_lock();
 callbacks.on_callback=[&]{EXPECT_TRUE(system.voice_scheduler.pause(env.mem,&other));};update();
 EXPECT_TRUE(other.is_paused);EXPECT_EQ(other.state,ngs::VOICE_STATE_FINALIZING);EXPECT_EQ(callbacks.calls,1);EXPECT_EQ(source->process_calls,0);
 EXPECT_TRUE(system.voice_scheduler.resume(env.mem,&other));update();EXPECT_EQ(other.state,ngs::VOICE_STATE_AVAILABLE);EXPECT_EQ(callbacks.calls,2);
}
TEST_F(NgsKeyOffFixture, FinishedCallbackStopsOtherVoiceInSameUpdate){
 auto*source=add_loop(false);voice.datas[1].is_bypassed=true;
 ngs::Voice other{};other.rack=&rack;other.state=ngs::VOICE_STATE_ACTIVE;other.voice_mutex=std::make_unique<std::mutex>();other.datas.resize(2);
 for(int i=0;i<2;i++){other.datas[i].parent=&other;other.datas[i].index=i;other.datas[i].info=voice.datas[i].info;}other.datas[1].is_bypassed=true;
 system.voice_scheduler.queue.push_back(&other);key_off_while_caller_owns_lock();
 callbacks.on_callback=[&]{EXPECT_TRUE(system.voice_scheduler.stop(env.mem,&other));};update();
 EXPECT_EQ(other.state,ngs::VOICE_STATE_AVAILABLE);EXPECT_EQ(source->process_calls,0);EXPECT_EQ(other.frame_count,0u);
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
