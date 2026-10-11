#!/usr/bin/env python3
"""Exercise the production input section against real TouchState and buffer IO."""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
native = root / 'app/src/main/cpp'
source = (native / 'emucorev/src/savestate/savestate.cpp').read_text(encoding='utf-8')
start = 'bool preflight_input(const std::vector<uint8_t> &data, std::string &error);'
end = 'bool preflight_io(const std::vector<uint8_t> &data, std::string &error);'
assert source.count(start) == source.count(end) == 1
production = source[source.index(start):source.index(end)]
motion = native / 'vita3k/vita3k/motion'
motion_runtime = (motion / 'src/motion.cpp').read_text(encoding='utf-8')
motion_runtime = motion_runtime[motion_runtime.index('static void detect_device_motion_support'):motion_runtime.index('SceFVector3 get_acceleration')]
out = root / 'app/build/input-restore-probe'
out.mkdir(parents=True, exist_ok=True)
cpp = r'''
#include <emucorev/savestate/state_io.h>
#include <emucorev/savestate/motion_state_io.h>
#include <touch/state.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <cstdio>
#define LOG_CRITICAL(...) ((void)0)
#define LOG_INFO(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
extern "C" {
void SDL_CloseSensor(SDL_Sensor*) {}
SDL_Sensor* SDL_OpenSensor(SDL_SensorID) { return nullptr; }
SDL_SensorID* SDL_GetSensors(int* count) { *count=0; return nullptr; }
SDL_SensorType SDL_GetSensorTypeForID(SDL_SensorID) { return SDL_SENSOR_UNKNOWN; }
void SDL_free(void* p) { free(p); }
const char* SDL_GetError() { return "no sensor"; }
Uint64 SDL_GetTicksNS() { return 100000; }
}
'''+motion_runtime+r'''
enum SceCtrlPadInputMode {SCE_CTRL_MODE_DIGITAL, SCE_CTRL_MODE_ANALOG, SCE_CTRL_MODE_ANALOG_WIDE};
struct EmuEnvState {
 struct {SceCtrlPadInputMode input_mode=SCE_CTRL_MODE_DIGITAL,input_mode_ext=SCE_CTRL_MODE_DIGITAL;uint64_t last_vcount[5]={};}ctrl;
 TouchState touch;
 MotionState motion;
 struct {std::atomic<uint64_t> vblank_count=15000;}display;
};
namespace emucorev::savestate {
enum class SectionId {Input};
struct Writer {std::vector<uint8_t> input;bool write_section(SectionId,const void *p,size_t n,bool,std::string&){input.assign((const uint8_t*)p,(const uint8_t*)p+n);return true;}};
'''+production+r'''
}
using namespace emucorev::savestate;
int main(){int failed=0;auto check=[&](bool ok,const char *n){printf("%s %s\n",ok?"PASS":"FAIL",n);failed+=!ok;};
 std::string error;
 BufferWriter old;old.i32(1);old.i32(2);for(int i=0;i<5;++i)old.u64(14990+i);
 EmuEnvState env;env.touch.last_vcount[0]=15893;env.touch.last_vcount[1]=17700;
 env.touch.touch_buffers[12][0].reportNum=1;env.touch.touch_buffers[12][0].report[0].x=999;env.touch.touch_buffer_idx=12;
 env.touch.finger_count=1;env.touch.touchpad_finger_count=1;env.touch.mouse_button_left=true;
 check(read_input(env,old.data(),error),"legacy input section remains loadable");
 check(env.touch.last_vcount[0]<=15000&&env.touch.last_vcount[1]<=15000,"legacy restore cannot leave future touch cursors that freeze sceTouchRead");
 check(env.touch.touch_buffer_idx==0&&env.touch.touch_buffers[12][0].reportNum==0,"legacy restore discards touch reports from abandoned timeline");
 check(env.touch.finger_count==0&&env.touch.touchpad_finger_count==0&&!env.touch.mouse_button_left,"restore releases live physical touches");
 check(env.ctrl.input_mode==SCE_CTRL_MODE_ANALOG&&env.ctrl.last_vcount[4]==14994,"controller input state preserved");
 EmuEnvState saved;saved.touch.touch_mode[0]=SCE_TOUCH_SAMPLING_STATE_START;saved.touch.force_touch_enabled[1]=true;
 saved.touch.last_vcount[0]=14997;saved.touch.last_vcount[1]=14999;saved.touch.touch_buffer_idx=63;
 saved.touch.curr_touch_id[0]=127;saved.touch.curr_touch_id[1]=42;
 saved.touch.touch_buffers[63][0].timeStamp=123456;saved.touch.touch_buffers[63][0].reportNum=1;
 saved.touch.touch_buffers[63][0].report[0].x=125;saved.touch.touch_buffers[63][0].report[0].y=88;
 Writer writer;check(write_input(saved,writer,error),"new input snapshot captures touch state");
 env.touch.touch_mode[0]=SCE_TOUCH_SAMPLING_STATE_STOP;env.touch.force_touch_enabled[1]=false;
 check(read_input(env,writer.input,error),"new input snapshot restores successfully");
 check(env.touch.last_vcount[0]==14997&&env.touch.last_vcount[1]==14999,"new snapshot restores saved cursors rather than live ones");
 check(env.touch.touch_mode[0]==SCE_TOUCH_SAMPLING_STATE_START&&env.touch.force_touch_enabled[1],"guest touch sampling and force settings round trip");
 check(env.touch.touch_buffer_idx==63&&env.touch.curr_touch_id[0]==127&&env.touch.touch_buffers[63][0].report[0].x==125&&env.touch.touch_buffers[63][0].timeStamp==123456,"guest touch ring and contact IDs round trip");
 auto truncated=writer.input;truncated.pop_back();check(!preflight_input(truncated,error),"truncated input state rejected before mutation");
 auto unsupported=writer.input;unsupported[48]=3;check(!preflight_input(unsupported,error),"unknown input extension rejected");
 auto touch_v1=writer.input;touch_v1[48]=1;touch_v1.resize(touch_v1.size()-198);
 check(preflight_input(touch_v1,error),"old touch-only version remains loadable");
 auto invalid_motion=writer.input;invalid_motion[invalid_motion.size()-198+4]=2;
 env.touch.last_vcount[0]=777;
 check(!read_input(env,invalid_motion,error)&&env.touch.last_vcount[0]==777,"malformed motion rejected before touch timeline mutation");
 env.touch.last_vcount[0]=777;check(!read_input(env,truncated,error)&&env.touch.last_vcount[0]==777,"invalid input cannot change live cursors");
 check(preflight_input_timeline(writer.input,15000,error),"saved input cursors fit display timeline");
 check(!preflight_input_timeline(writer.input,14996,error),"future saved touch cursor rejected before destructive restoration");
 check(!preflight_input_timeline(old.data(),14989,error),"future saved controller cursor also rejected");
 for(int i=0;i<5;++i){env.touch.last_vcount[0]=50000+i;check(read_input(env,old.data(),error)&&env.touch.last_vcount[0]==0,"repeated legacy loads never inherit future input cursor");}
 saved.touch.touch_buffer_idx=64;Writer invalid;check(!write_input(saved,invalid,error),"out of bounds touch ring rejected");
 saved.touch.touch_buffer_idx=0;saved.touch.touch_buffers[0][0].reportNum=9;check(!write_input(saved,invalid,error),"out of bounds report count rejected");
 return failed?1:0;
}
'''
(out / 'probe.cpp').write_text(cpp, encoding='utf-8')
if os.name == 'nt':
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler, 'host C++ compiler required'
includes = [native / 'emucorev/include', native / 'vita3k/vita3k/touch/include',
            native / 'vita3k/vita3k/util/include', native / 'vita3k/external/sdl/include']
includes += [motion / 'include', native / 'vita3k/external/fmt/include']
subprocess.run([compiler, '-std=c++20', '-O0', *['-I'+str(p) for p in includes],
                str(out/'probe.cpp'), str(motion / 'src/motion_input.cpp'), '-o', str(out/'probe.exe')], check=True)
result = subprocess.run([str(out/'probe.exe')], capture_output=True, text=True)
print(result.stdout, end='')
(out/'result.log').write_text(result.stdout+result.stderr)
raise SystemExit(result.returncode)
