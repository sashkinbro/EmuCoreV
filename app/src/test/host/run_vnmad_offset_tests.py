#!/usr/bin/env python3
"""Check production VNMAD operand addressing, with numeric load/store seams."""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
shader = root / 'app/src/main/cpp/vita3k/vita3k/shader'
alu = (shader / 'src/translator/alu.cpp').read_text(encoding='utf-8')
header = (shader / 'include/shader/usse_translator.h').read_text(encoding='utf-8')
helpers = header[header.index('#define BEGIN_REPEAT'):header.index('    void reset_repeat_increase()')]
loads = alu[alu.index('spv::Id USSETranslatorVisitor::do_alu_op('):alu.index('    std::vector<spv::Id> ids;', alu.index('spv::Id USSETranslatorVisitor::do_alu_op('))]
loads = loads.replace('USSETranslatorVisitor::', '') + 'return vsrc1 + vsrc2;\n}\n'
function = alu[alu.index('bool USSETranslatorVisitor::v32nmad('):alu.index('bool USSETranslatorVisitor::v16nmad(')]
emission = function[function.index('    // Recompile'):]
cpp = r'''
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>
using Imm4 = uint8_t;
namespace spv { using Id = int; constexpr Id NoResult = 0; }
enum class RegisterBank { TEMP, PRIMATTR, SECATTR, FPINTERNAL, PREDICATE, INDEXED1, INDEXED2 };
enum RegisterFlags { GPI = 1 };
enum class RepeatMode { INTERNAL, BOTH, EXTERNAL, SLMSI };
struct Operand { int index = 0; RegisterBank bank = RegisterBank::TEMP; int flags = 0; };
struct Instruction { struct { Operand dest{3},src0{0},src1{1},src2{2}; } opr; };
#define LOG_DISASM(...) ((void)0)
struct Probe {
    int repeat_increase[4][17]{};
    int repeat_multiplier[4]{1,1,1,1};
    Instruction inst;
    Imm4 source_mask = 15, dest_mask = 15;
    std::vector<int> loaded_offsets;
    int stored_offset = -999, stored_value = 0;
    struct Builder { void setDebugSourceLocation(int, void*) {} } m_b;
    struct Recompiler { int cur_pc = 0; } m_recompiler;
    spv::Id load(Operand, Imm4, int offset) { loaded_offsets.push_back(offset); return 123; }
    void store(Operand, spv::Id value, Imm4, int offset) { stored_offset=offset; stored_value=value; }
''' + helpers + loads + 'bool run() {\n' + emission + r'''
};
int main() {
    int failures=0;
    auto check=[&](bool ok,const char*name){printf("%s %s\n",ok?"PASS":"FAIL",name); failures+=!ok;};
    for (int offset : {0, 1, 7, -3}) {
        Probe p;
        p.repeat_increase[1][0]=offset;
        p.repeat_increase[2][0]=offset+2;
        p.repeat_increase[3][0]=offset-1;
        check(p.run(), "VNMAD translation succeeds");
        check(p.loaded_offsets==std::vector<int>({offset*2,(offset+2)*2}), "both sources honor repeat-slot-zero offsets");
        check(p.stored_offset==(offset-1)*2, "destination honors repeat-slot-zero offset");
        check(p.stored_value==246, "result is stored once");
        check(std::all_of(p.repeat_multiplier,p.repeat_multiplier+4,[](int v){return v==2;}), "repeat multipliers reset after instruction");
    }
    Probe zero; zero.run();
    check(zero.loaded_offsets==std::vector<int>({0,0}) && zero.stored_offset==0, "ordinary zero offsets preserve addressing");
    return failures?1:0;
}
'''
out = root / 'app/build/vnmad-offset-probe'
out.mkdir(parents=True, exist_ok=True)
(out / 'probe.cpp').write_text(cpp, encoding='utf-8')
if os.name == 'nt':
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler, 'host C++ compiler required'
subprocess.run([compiler, '-std=c++20', str(out / 'probe.cpp'), '-o', str(out / 'probe.exe')], check=True)
result = subprocess.run([str(out / 'probe.exe')], capture_output=True, text=True)
print(result.stdout + result.stderr, end='')
(out / ('red.log' if result.returncode else 'green.log')).write_text(result.stdout + result.stderr)
raise SystemExit(result.returncode)
