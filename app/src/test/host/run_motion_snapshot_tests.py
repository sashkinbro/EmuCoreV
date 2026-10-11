#!/usr/bin/env python3
"""Real motion/filter state and production Input codec; SDL handles are host seams."""
from pathlib import Path
import os,subprocess,shutil,sys
root=Path(__file__).resolve().parents[4];native=root/'app/src/main/cpp';motion=native/'vita3k/vita3k/motion'
source=(native/'emucorev/src/savestate/savestate.cpp').read_text(encoding='utf-8')
start='bool preflight_input(const std::vector<uint8_t> &data, std::string &error);';end='bool preflight_io(const std::vector<uint8_t> &data, std::string &error);'
production=source[source.index(start):source.index(end)]
if '--dto' in sys.argv:
 production='''bool write_input(EmuEnvState &env, Writer &writer, std::string &error){auto s=env.motion.capture_snapshot();if(!MotionState::validate_snapshot(s,error))return false;BufferWriter b;write_motion_snapshot(b,s);return writer.write_section(SectionId::Input,b.data().data(),b.size(),true,error);}
bool read_input(EmuEnvState &env,const std::vector<uint8_t>&data,std::string&error){BufferReader r(data.data(),data.size());MotionState::Snapshot s;if(!read_motion_snapshot(r,s,error)||r.remaining())return false;return env.motion.apply_snapshot(s,error);}'''
motion_source=(motion/'src/motion.cpp').read_text(encoding='utf-8');methods=motion_source[motion_source.index('static void detect_device_motion_support'):motion_source.index('SceFVector3 get_acceleration')]
events=motion_source[motion_source.index('constexpr uint64_t to_microseconds'):]
out=root/'app/build/motion-snapshot-probe';out.mkdir(exist_ok=True)
cpp=r'''
#include <emucorev/savestate/state_io.h>
#include <motion/state.h>
#include <SDL3/SDL_timer.h>
#include <chrono>
#include <numbers>
#include <future>
#if __has_include(<emucorev/savestate/motion_state_io.h>)
#include <emucorev/savestate/motion_state_io.h>
#endif
#include <touch/state.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <cstdio>
#define LOG_CRITICAL(...) ((void)0)
#define LOG_INFO(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
unsigned opened=0,closed=0;bool fail_open=false;
extern "C" { Uint64 SDL_GetTicksNS(){return 1000000;} void SDL_CloseSensor(SDL_Sensor*) {++closed;} SDL_Sensor *SDL_OpenSensor(SDL_SensorID id) {++opened;return fail_open?nullptr:reinterpret_cast<SDL_Sensor*>(uintptr_t(id+100));} SDL_SensorID* SDL_GetSensors(int*n){*n=0;return nullptr;} SDL_SensorType SDL_GetSensorTypeForID(SDL_SensorID){return SDL_SENSOR_UNKNOWN;} void SDL_free(void*p){free(p);} const char* SDL_GetError(){return "no sensor";} }
'''+methods+r'''
enum SceCtrlPadInputMode {SCE_CTRL_MODE_DIGITAL,SCE_CTRL_MODE_ANALOG,SCE_CTRL_MODE_ANALOG_WIDE};
struct CtrlState {SceCtrlPadInputMode input_mode=SCE_CTRL_MODE_DIGITAL,input_mode_ext=SCE_CTRL_MODE_DIGITAL;uint64_t last_vcount[5]={};bool has_motion_support=false;};
struct EmuEnvState {CtrlState ctrl;TouchState touch;MotionState motion;struct{std::atomic<uint64_t> vblank_count=15000;}display;};
'''+events+r'''
namespace emucorev::savestate {enum class SectionId{Input};struct Writer{std::vector<uint8_t>input;bool write_section(SectionId,const void*p,size_t n,bool,std::string&){input.assign((const uint8_t*)p,(const uint8_t*)p+n);return true;}};
'''+production+r'''
}
using namespace emucorev::savestate;
int main(){unsigned failed=0;auto check=[&](bool ok,const char*n){printf("%s %s\n",ok?"PASS":"FAIL",n);failed+=!ok;};EmuEnvState saved,live;std::string error;
saved.motion.start_sensor_sampling();saved.motion.motion_data.EnableGyroBias(false);saved.motion.motion_data.EnableDeadband(false);saved.motion.motion_data.EnableTiltCorrection(false);saved.motion.motion_data.SetAngleThreshold(13);saved.motion.motion_data.RotateYaw(0.75f);saved.motion.last_counter=42;const auto quat=saved.motion.motion_data.GetOrientation();Writer w;check(write_input(saved,w,error),"save warm motion state");
live.motion.stop_sensor_sampling();live.motion.motion_data.SetAngleThreshold(42);live.motion.last_counter=99;check(read_input(live,w.input,error),"load warm input");check(live.motion.is_sampling,"sampling restored for cold state");check(!live.motion.motion_data.IsGyroBiasEnabled()&&!live.motion.motion_data.IsDeadbandEnabled()&&!live.motion.motion_data.IsTiltCorrectionEnabled()&&live.motion.motion_data.GetAngleThreshold()==13,"guest correction settings restored");const auto actual=live.motion.motion_data.GetOrientation();check(actual.xyz.x==quat.xyz.x&&actual.xyz.y==quat.xyz.y&&actual.xyz.z==quat.xyz.z&&actual.w==quat.w,"guest yaw orientation restored");check(live.motion.last_counter==42,"sensor counter restored");
saved.motion.stop_sensor_sampling();Writer stopped;check(write_input(saved,stopped,error),"save stopped sampling");live.motion.start_sensor_sampling();check(read_input(live,stopped.input,error)&&!live.motion.is_sampling,"stopped snapshot turns sampling off");
#ifdef MOTION_DTO_TESTS
 auto snapshot=saved.motion.capture_snapshot();snapshot.is_sampling=true;snapshot.last_accel_timestamp=100;snapshot.last_updated_accel_timestamp=110;snapshot.last_gyro_timestamp=200;snapshot.last_updated_gyro_timestamp=220;
 snapshot.data.real_error={.1f,.2f,.3f};snapshot.data.integral_error={.4f,.5f,.6f};snapshot.data.derivative_error={.7f,.8f,.9f};snapshot.data.gyro_bias={.01f,.02f,.03f};snapshot.data.rotations={1,2,3};snapshot.data.accel={0,0,-1};snapshot.data.gyro={.1f,.2f,.3f};snapshot.data.basic_orientation={-1,0,1};snapshot.data.basic_orientation_base={0,-1,0};snapshot.data.reset_counter=100;snapshot.data.only_accelerometer=false;
 MotionState restored;check(restored.apply_snapshot(snapshot,error),"apply full numerical motion state");BufferWriter first,second;write_motion_snapshot(first,snapshot);write_motion_snapshot(second,restored.capture_snapshot());check(first.data()==second.data(),"every encoded field survives capture/apply exactly");check(first.size()==198,"bounded explicit motion wire layout");
 auto before=restored.capture_snapshot();auto bad=snapshot;bad.data.quat.w=std::numeric_limits<float>::quiet_NaN();check(!restored.apply_snapshot(bad,error)&&restored.capture_snapshot().last_counter==before.last_counter,"invalid numerical snapshot rejected before state mutation");
 bad=snapshot;bad.last_updated_accel_timestamp=99;bad.last_counter=1000;check(!restored.apply_snapshot(bad,error)&&restored.capture_snapshot().last_counter==before.last_counter,"clock preceding consumed sample rejected before mutation");
 auto bytes=first.data();bytes[4]=2;BufferReader invalid(bytes.data(),bytes.size());auto unchanged=before;check(!read_motion_snapshot(invalid,unchanged,error)&&unchanged.last_counter==before.last_counter,"invalid bool codec rejected atomically");
 bytes=first.data();bytes.pop_back();BufferReader truncated(bytes.data(),bytes.size());check(!read_motion_snapshot(truncated,unchanged,error),"truncated snapshot rejected");
 restored.has_device_motion_support=true;restored.device_accel_id=7;restored.device_gyro_id=9;restored.device_native_rotation=ROTATION_90;restored.start_sensor_sampling();auto*accel=restored.device_accel.get();auto*gyro=restored.device_gyro.get();auto num_open=opened;check(restored.apply_snapshot(snapshot,error)&&restored.device_accel.get()==accel&&restored.device_gyro.get()==gyro&&opened==num_open&&restored.device_accel_id==7&&restored.device_native_rotation==ROTATION_90,"active restore preserves live sensor handles and hardware configuration");
 snapshot.is_sampling=false;auto num_closed=closed;check(restored.apply_snapshot(snapshot,error)&&closed==num_closed+2&&!restored.is_sampling,"stopped restore closes existing physical sensors once");snapshot.is_sampling=true;restored.has_device_motion_support=true;fail_open=true;check(restored.apply_snapshot(snapshot,error)&&restored.is_sampling&&!restored.has_device_motion_support&&!restored.device_accel&&!restored.device_gyro,"physical reopen failure retains logical sampling with neutral fallback");fail_open=false;
 check(restored.accel_clock.map(9000000,110)==110&&restored.accel_clock.map(9000016,110)==126,"first new sensor establishes saved logical clock and excludes load downtime");check(restored.accel_clock.map(9000008,126)==126,"out-of-order sensor cannot move logical clock backwards");check(restored.accel_clock.map(8999990,126)==126,"backward host clock rebases without unsigned integration overflow");
 EmuEnvState sensor_env;sensor_env.ctrl.has_motion_support=true;check(sensor_env.motion.apply_snapshot(snapshot,error),"restore before receiving sensor events");SDL_SensorEvent event{};event.type=SDL_EVENT_SENSOR_UPDATE;event.timestamp=999999;event.sensor_timestamp=9000000000;event.data[0]=SDL_STANDARD_GRAVITY;handle_motion_event(sensor_env,SDL_SENSOR_ACCEL,event);check(sensor_env.motion.motion_data.GetAcceleration().x==0,"queued pre-restore motion event cannot mutate restored filter");event.timestamp=1000001;handle_motion_event(sensor_env,SDL_SENSOR_ACCEL,event);check(sensor_env.motion.last_updated_accel_timestamp==110&&sensor_env.motion.motion_data.GetAcceleration().x==-1,"first post-restore event uses saved clock origin");event.sensor_timestamp+=16667000;handle_motion_event(sensor_env,SDL_SENSOR_ACCEL,event);check(sensor_env.motion.last_updated_accel_timestamp==16777,"later sensor event advances by real sample interval");
 auto legacy=MotionState::legacy_snapshot();check(legacy.is_sampling&&legacy.data.angle_threshold==20&&legacy.data.quat.xyz.z==-1,"legacy policy explicitly neutral active sampling");
#endif
 printf("%u failures\n",failed);return failed?1:0;}
'''
(out/'probe.cpp').write_text(cpp,encoding='utf-8')
os.environ['PATH']='C:/msys64/ucrt64/bin;'+os.environ['PATH'];compiler=shutil.which('g++');assert compiler
includes=[native/'emucorev/include',motion/'include',native/'vita3k/vita3k/touch/include',native/'vita3k/vita3k/util/include',native/'vita3k/external/sdl/include',native/'vita3k/external/fmt/include']
subprocess.run([compiler,'-std=c++20','-O0',*(['-DMOTION_DTO_TESTS'] if '--dto' in sys.argv else []),*['-I'+str(p)for p in includes],str(out/'probe.cpp'),str(motion/'src/motion_input.cpp'),'-o',str(out/'probe.exe')],check=True)
p=subprocess.run([str(out/'probe.exe')],capture_output=True,text=True);print(p.stdout,end='');(out/'result.log').write_text(p.stdout+p.stderr);raise SystemExit(p.returncode)
