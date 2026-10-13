#!/usr/bin/env python3
"""Run the real sceImeUpdate body with a guest callback/memory allocation seam."""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
core = root / 'app/src/main/cpp/vita3k/vita3k'
external = core.parent / 'external'
out = root / 'app/build/ime-callback-probe'
out.mkdir(parents=True, exist_ok=True)
if os.name == 'nt':
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
def block(path, marker):
    source = path.read_text()
    start = source.index(marker)
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return source[start:end + 1]
    raise AssertionError(marker)

module = core / 'modules/SceIme/SceIme.cpp'
body = block(module, 'EXPORT(SceInt32, sceImeUpdate)').replace(
    'EXPORT(SceInt32, sceImeUpdate)', 'SceInt32 update(TestEnv &emuenv)')
opening = block(module, 'EXPORT(SceInt32, sceImeOpen,').replace(
    'EXPORT(SceInt32, sceImeOpen, SceImeParam *param)',
    'SceInt32 open_ime(TestEnv &emuenv, SceImeParam *param)')
submit = block(core / 'android/jni/android_state.cpp', 'bool submit_current_ime(').replace(
    'EmuEnvState', 'TestEnv').replace(', bool finish_keyboard)', ', bool finish_keyboard = true)')
cpp = r'''
#include <ime/state.h>
#include <cstring>
#include <cstdio>
#include <functional>
#include <thread>
#include <vector>
#define NDEBUG
#define LOG_INFO(...)
#define TRACY_FUNC(...)
#define RET_ERROR(value) (value)
struct TestEnv {Ime ime; MemState mem;};
Address alloc(MemState &, uint32_t, const char *, Address) {return 256;}
void free(MemState &, Address) {}
std::function<void(TestEnv &, const SceImeEvent &)> guest_callback;
#define CALL_EXPORT(name, arg, event) guest_callback(emuenv, *event)
bool is_any_ime_active(TestEnv &e) {return e.ime.state;}
bool is_ime_dialog_active(const TestEnv &) {return false;}
void finish_ime_dialog(TestEnv &) {std::abort();}
''' + opening + '\n' + submit + '\n' + body + r'''
int main() {
 int failed=0;
 auto check=[&](bool ok,const char *name){
  std::printf("%s %s\n",ok?"PASS":"FAIL",name);failed+=!ok;};
 TestEnv env;
 env.mem.memory=Memory(new uint8_t[4096]{},[](uint8_t *p){delete[] p;});
 SceImeParam param{};
 param.maxTextLength=32;
 param.inputTextBuffer=Ptr<SceWChar16>(1024);
 int opened=0;
 guest_callback=[&](TestEnv &,const SceImeEvent &event){opened+=event.id==SCE_IME_EVENT_OPEN;};
 check(open_ime(env,&param)==0,"IME opens successfully");
 check(update(env)==0,"first IME update succeeds");
 check(opened==1,"guest receives the Open event needed to initialize its editor");
 std::vector<uint32_t> completion;
 guest_callback=[&](TestEnv &,const SceImeEvent &event){completion.push_back(event.id);};
 check(submit_current_ime(env),"Android Done is accepted");
 update(env);update(env);
 check(completion==std::vector<uint32_t>{SCE_IME_EVENT_PRESS_ENTER,SCE_IME_EVENT_PRESS_CLOSE},
       "Android Done delivers Enter then Close in separate guest updates");
 completion.clear();
 check(submit_current_ime(env,false),"hardware Enter is accepted");
 update(env);update(env);
 check(completion==std::vector<uint32_t>{SCE_IME_EVENT_PRESS_ENTER},
       "hardware Enter does not force keyboard closure");
 env.ime.state=true;
 env.ime.str=u"Lenovo";
 env.ime.caretIndex=6;
 env.ime.edit_text.str=Ptr<SceWChar16>(1024);
 env.ime.edit_text.preeditIndex=2;
 env.ime.edit_text.caretIndex=6;
 env.ime.event_id=SCE_IME_EVENT_PRESS_ENTER;
 guest_callback=[&](TestEnv &e,const SceImeEvent &event){
  bool unlocked=false;
  std::thread probe([&]{unlocked=e.ime.mutex.try_lock();if(unlocked)e.ime.mutex.unlock();});
  probe.join();
  check(unlocked,"guest Enter callback can close IME without deadlocking snapshot notification");
  check(event.id==SCE_IME_EVENT_PRESS_ENTER,"Enter reaches guest callback");
  check(std::u16string(reinterpret_cast<char16_t *>(e.ime.edit_text.str.get(e.mem)))==u"Lenovo",
        "Enter callback sees final committed text");
  if(unlocked) {
   std::lock_guard lock(e.ime.mutex);
   // A reentrant guest callback can change text/caret or open the next editor.
   e.ime.event_id=SCE_IME_EVENT_UPDATE_CARET;
  }
 };
 check(update(env)==0,"Enter update returns successfully");
 check(env.ime.event_id==SCE_IME_EVENT_UPDATE_CARET,"reentrant callback event is not discarded");
 env.ime.event_id=SCE_IME_EVENT_UPDATE_TEXT;
 guest_callback=[&](TestEnv &,const SceImeEvent &event){
  check(event.param.text.preeditIndex==2,"text event preserves preedit index instead of overwriting union with caret");
  check(event.param.text.caretIndex==6,"text event carries caret in text payload");
 };
 check(update(env)==0,"text update succeeds");
 env.ime.event_id=SCE_IME_EVENT_UPDATE_CARET;
 guest_callback=[&](TestEnv &,const SceImeEvent &event){
  check(event.param.caretIndex==6,"caret event uses caret payload");
 };
 check(update(env)==0,"caret update succeeds");
 return failed?1:0;
}
'''
(out / 'probe.cpp').write_text(cpp)
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler, 'A C++20 compiler is required'
includes = [core / (part + '/include') for part in ('ime', 'mem', 'util')]
includes += [external / 'boost']
command = [compiler, '-std=c++20', '-O0', '-pthread']
command += ['-I' + str(p) for p in includes]
command += [str(out / 'probe.cpp'), '-o', str(out / 'probe.exe')]
subprocess.run(command, check=True)
result = subprocess.run([str(out / 'probe.exe')], capture_output=True, text=True, timeout=10)
print(result.stdout + result.stderr)
(out / ('red.log' if result.returncode else 'green.log')).write_text(result.stdout + result.stderr)
raise SystemExit(result.returncode)
