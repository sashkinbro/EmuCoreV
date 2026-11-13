#!/usr/bin/env python3
"""Exercise the real cheat interpreter and JNI setters with bounded host memory.

Only guest memory, parser/file lookup and JNI environment are seams. The complete
production interpreter, state transitions and extracted JNI setter bodies run.
--candidate points to an ignored candidate directory during the native build lease.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
args = argparse.ArgumentParser()
args.add_argument('--candidate', type=Path)
options = args.parse_args()
core = root / 'app/src/main/cpp/vita3k/vita3k'
work = root / 'tools/cheats/sol-implementation/native-probe'
work.mkdir(parents=True, exist_ok=True)
candidate = options.candidate
interpreter_path = candidate / 'cheat.cpp' if candidate else core / 'cheat/src/cheat.cpp'
bridge_path = candidate / 'cheat_bridge.cpp' if candidate else root / 'app/src/main/cpp/emucorev/src/cheat_bridge.cpp'
interpreter = interpreter_path.read_text(encoding='utf-8-sig')
bridge = bridge_path.read_text(encoding='utf-8-sig')

def function(source, marker):
    assert source.count(marker) == 1, marker
    start = source.index(marker)
    brace = source.index('{', start)
    depth = 0
    for end in range(brace, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return source[start:end + 1]
    raise AssertionError(marker)

support = work / 'include'
(support / 'util').mkdir(parents=True, exist_ok=True)
(support / 'util/fs.h').write_text('#pragma once\n#include <filesystem>\nnamespace fs=std::filesystem;\n')
cpp = r'''
#include <cheat/functions.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <limits>
constexpr uint32_t KiB(uint32_t n) { return n * 1024; }
struct MemState {
    std::array<uint8_t, 65536> bytes{};
    bool use_page_table = false;
    std::array<uint8_t*, 16> page_table{};
};
bool is_valid_addr_range(const MemState&, uint32_t a, uint32_t b) {
    return a < b && b <= 65536;
}
template<class T> struct Ptr {
    uint32_t address;
    explicit Ptr(uint32_t a): address(a) {}
    T* get(const MemState& m) const { return (T*)&m.bytes[address]; }
};
#define LOG_WARN_ONCE(...) ((void)0)
#define LOG_DEBUG(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
#define LOG_INFO(...) ((void)0)
namespace cheat {
fs::path find_cheat_file(const fs::path&, const std::string&) { return {}; }
CheatFile parse_cheat_file(const fs::path&, const std::string&) { return {}; }
bool save_cheat_file(const CheatFile&) { return true; }
}
'''
cpp += '\n'.join(line for line in interpreter.splitlines() if not line.startswith('#include'))
cpp += r'''
using jboolean=bool; using jstring=const char*; using jobject=int; using jint=int;
struct JNIEnv {};
constexpr bool JNI_TRUE=true, JNI_FALSE=false;
#define JNIEXPORT
#define JNICALL
struct EmuEnvState { CheatState cheat; MemState mem; };
EmuEnvState* current=nullptr;
EmuEnvState* get_emuenv() { return current; }
std::string jstring_to_string(JNIEnv*, jstring text) { return text ? text : ""; }
bool is_live_title(EmuEnvState*, const std::string& title) { return title == "PCSA00029"; }
cheat::CheatFile load_cheat_file(EmuEnvState&, const std::string&) { return {}; }
cheat::JitInvalidate make_invalidate(EmuEnvState&) { return {}; }
namespace emucorev::savestate {
std::mutex& save_state_operation_mutex() { static std::mutex mutex; return mutex; }
}
'''
for name in ['setCheatEnabled', 'setAllCheatsEnabled']:
    cpp += '\n' + function(bridge, 'extern "C" JNIEXPORT jboolean JNICALL\nJava_com_sbro_emucorev_core_CheatBridge_' + name + '(')
cpp += r'''
int checks=0, failures=0;
void check(bool value, const char* name) {
    ++checks; if (!value) { ++failures; std::printf("FAIL %s\n", name); }
}
void put(MemState& mem, uint32_t address, uint32_t value) { std::memcpy(mem.bytes.data()+address, &value, 4); }
uint32_t get(const MemState& mem, uint32_t address) { uint32_t value; std::memcpy(&value, mem.bytes.data()+address, 4); return value; }
void install(CheatState& state, std::initializer_list<cheat::CodeLine> lines) {
    state.enabled=true; state.file={}; cheat::Cheat item;
    item.name="test"; item.enabled=true; item.lines=lines; state.file.cheats.push_back(item);
}
void rejected(CheatState& state, MemState& mem, const char* label) {
    cheat::apply(state, mem, {});
    check(state.file.cheats[0].broken, label);
    check(!state.file.cheats[0].enabled, "failed cheat is not reported enabled");
    check(cheat::enabled_cheat_count(state)==0, "failed cheat excluded from enabled count");
}
int main() {
    EmuEnvState env; current=&env; auto& state=env.cheat; auto& mem=env.mem;
    install(state, {{0x0200, 0, 99}}); rejected(state,mem,"null direct write is broken");
    install(state, {{0x0200, 0xFFFE, 99}}); rejected(state,mem,"boundary-crossing direct write is broken");
    check(get(mem,0xFFFC)==0,"failed crossing write changes no bytes");
    install(state, {{0x0200,0x5000,0x12345678},{0x0100,0x5004,0xABCD},{0x0000,0x5006,0xEF}});
    cheat::apply(state,mem,{});
    check(get(mem,0x5000)==0x12345678 && get(mem,0x5004)==0x00EFABCD,"valid 8/16/32-bit writes still execute");
    check(state.file.cheats[0].enabled && !state.file.cheats[0].broken,"valid cheat stays enabled");
    state.modules.clear(); CheatModule module; module.segments[0]={0xFFFFF000,0x1000}; cheat::add_module(state,module);
    put(mem,0x5000,0);
    install(state, {{0xB200,0,0},{0x0200,0x6000,99}}); rejected(state,mem,"relative base cannot wrap to mapped memory");
    check(get(mem,0x5000)==0,"overflow does not write wrapped mapped address");
    put(mem,0x6000,0);
    install(state, {{0x4201,0xFFFC,1},{0x0002,4,1},{0x0200,0x6000,77}});
    rejected(state,mem,"repeat destination failure is broken");
    check(get(mem,0x6000)==0,"failing repeat stops later lines");
    install(state, {{0x4201,0x9000,1},{0x0002,0xFFFFE000,0}});
    rejected(state,mem,"repeat address arithmetic cannot wrap");
    install(state, {{0x3201,0x10000,0},{0x3300,0,99}}); rejected(state,mem,"pointer read failure is broken");
    put(mem,0x5100,0xFFFE);
    install(state, {{0x3201,0x5100,0},{0x3300,0,99}}); rejected(state,mem,"pointer write failure is broken");
    put(mem,0x5100,0xFFFFFFF0);
    install(state, {{0x3201,0x5100,0x6010},{0x3300,0,99}}); rejected(state,mem,"pointer addition cannot wrap");
    put(mem,0x5100,0xFFFC);
    install(state, {{0x7201,0x5100,0},{0x7700,0,5},{0x0002,4,1}}); rejected(state,mem,"pointer repeat failure is broken");
    install(state, {{0x5200,0x6000,0x10000}}); rejected(state,mem,"copy source failure is broken");
    install(state, {{0x5200,0x10000,0x6000}}); rejected(state,mem,"copy destination failure is broken");
    put(mem,0x5100,0x10000); put(mem,0x5200,0x6000);
    install(state, {{0x8201,0x5100,0},{0x8800,0,0},{0x8601,0x5200,0},{0x8900,0,0}});
    rejected(state,mem,"pointer copy destination failure is broken");
    install(state, {{0xA200,0x10000,0xE1A00000}}); rejected(state,mem,"ARM inaccessible source is broken");
    put(mem,0x6000,0x11223344); unsigned invalidated=0;
    install(state, {{0xA200,0x6000,0xE1A00000},{0x0200,0,5}});
    cheat::apply(state,mem,[&](uint32_t,size_t){++invalidated;});
    check(state.file.cheats[0].broken && !state.file.cheats[0].enabled,"later write failure disables ARM cheat");
    check(get(mem,0x6000)==0x11223344 && state.file.cheats[0].saved_memory.empty(),"later failure restores original ARM code immediately");
    check(invalidated==2,"patch and failure restoration both invalidate JIT");
    install(state, {{0x0200,0x6000,1}});
    check(!Java_com_sbro_emucorev_core_CheatBridge_setCheatEnabled(nullptr,0,"PCSA00029",99,true),"JNI rejects stale live cheat index");
    state.file.cheats.clear();
    check(!Java_com_sbro_emucorev_core_CheatBridge_setAllCheatsEnabled(nullptr,0,"PCSA00029",true),"JNI rejects empty live group enable");
    install(state, {{0x0200,0,1}}); cheat::apply(state,mem,{});
    check(!Java_com_sbro_emucorev_core_CheatBridge_setCheatEnabled(nullptr,0,"PCSA00029",0,true),"failed cheat needs reload rather than false retry success");
    check(!state.file.cheats[0].enabled,"failed retry remains off");
    cheat::Cheat valid; valid.lines={{0x0200,0x6000,7}}; state.file.cheats.push_back(valid);
    check(Java_com_sbro_emucorev_core_CheatBridge_setAllCheatsEnabled(nullptr,0,"PCSA00029",true),"group enable accepts runnable entries");
    check(!state.file.cheats[0].enabled && state.file.cheats[1].enabled,"group enable leaves broken entries off");
    std::printf("%s real interpreter/JNI: %d checks, %d failures\n",failures?"FAIL":"PASS",checks,failures);
    return failures ? 1 : 0;
}
'''
(work / 'probe.cpp').write_text(cpp, encoding='utf-8')
if os.name == 'nt':
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
cxx = os.environ.get('CXX') or shutil.which('g++')
assert cxx, 'Host C++ compiler missing'
includes = [support]
if candidate:
    includes.append(candidate / 'include')
includes.append(core / 'cheat/include')
subprocess.run([cxx, '-std=c++20', '-pthread', '-O0', *['-I' + str(p) for p in includes], str(work/'probe.cpp'), '-o', str(work/'probe.exe')], check=True)
result = subprocess.run([str(work/'probe.exe')], capture_output=True, text=True, timeout=10)
print(result.stdout + result.stderr)
(work / ('candidate.log' if candidate else 'baseline.log')).write_text(result.stdout + result.stderr)
raise SystemExit(result.returncode)
