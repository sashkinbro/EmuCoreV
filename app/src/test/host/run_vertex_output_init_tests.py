#!/usr/bin/env python3
"""Check production output-register initialization using the real SPIR-V Builder.

The generated instructions must cover every vec4 with zero using constant
indices. A dynamic private-array initialization loop crashes stock Adreno 750
when compiling the locally captured NFS vertex shader; game bytes are not
included in this regression test.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
core = root / 'app/src/main/cpp/vita3k/vita3k'
glslang = root / 'app/src/main/cpp/vita3k/external/glslang'
out = root / 'app/build/vertex-output-init-probe'
out.mkdir(parents=True, exist_ok=True)

def block(source, marker):
    start = source.index(marker)
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if not depth:
            return source[start:end + 1]
    raise AssertionError(marker)

shader = (core / 'shader/src/spirv_recompiler.cpp').read_text()
initialization = block(shader[shader.index('// Initialize vertex output to 0'):], 'if (program.is_vertex())')
count = re.search(r'static constexpr int REG_O_COUNT = .*?;', shader).group()
access = block((core / 'shader/src/usse_utilities.cpp').read_text(), 'spv::Id create_access_chain(')
loop = block((core / 'shader/include/shader/usse_utilities.h').read_text(), 'void make_for_loop(')
cpp = r'''
#include <SPIRV/SpvBuilder.h>
#include <array>
#include <cstdio>
#include <map>
#include <set>
#include <vector>
namespace utils {
''' + access + '\ntemplate<typename F>\n' + loop + '\n}\n' + count + r'''
int main() {
    spv::SpvBuildLogger logger;
    spv::Builder b(spv::Spv_1_0, 0, &logger);
    b.setMemoryModel(spv::AddressingModelLogical, spv::MemoryModelGLSL450);
    b.addCapability(spv::CapabilityShader);
    auto *main = b.makeEntryPoint("main_vs");
    const auto vec4 = b.makeVectorType(b.makeFloatType(32), 4);
    const auto bank = b.makeArrayType(vec4, b.makeIntConstant(REG_O_COUNT / 4), 0);
    struct { spv::Id outs; } parameters{b.createVariable(spv::NoPrecision, spv::StorageClassPrivate, bank, "outs")};
    struct { bool is_vertex() const { return true; } } program;
''' + initialization + r'''
    b.makeReturn(false);
    b.leaveFunction();
    b.addEntryPoint(spv::ExecutionModelVertex, main, "main_vs");
    std::vector<unsigned> code;
    b.dump(code);
    std::map<unsigned, unsigned> constants;
    std::map<unsigned, std::vector<unsigned>> composites;
    std::map<unsigned, unsigned> destinations;
    std::set<unsigned> initialized;
    unsigned loops = 0, stores = 0;
    bool values_are_zero = true;
    for (size_t i = 5; i < code.size();) {
        const unsigned words = code[i] >> 16, op = code[i] & 65535;
        if (op == spv::OpConstant && words == 4) constants[code[i+2]] = code[i+3];
        if (op == spv::OpConstantComposite) composites[code[i+2]] = {code.begin()+i+3, code.begin()+i+words};
        if (op == spv::OpLoopMerge) ++loops;
        if (op == spv::OpAccessChain && words == 5 && code[i+3] == parameters.outs && constants.contains(code[i+4]))
            destinations[code[i+2]] = constants.at(code[i+4]);
        if (op == spv::OpStore && destinations.contains(code[i+1])) {
            ++stores;
            initialized.insert(destinations.at(code[i+1]));
            const auto value = composites.find(code[i+2]);
            if (value == composites.end() || value->second.size() != 4) values_are_zero = false;
            else for (auto scalar : value->second)
                if (!constants.contains(scalar) || constants.at(scalar) != 0) values_are_zero = false;
        }
        i += words;
    }
    const unsigned count = REG_O_COUNT / 4;
    const bool bounded = initialized.size() == count && *initialized.begin() == 0 && *initialized.rbegin() == count-1;
    printf("loops=%u constant stores=%u initialized=%zu zero=%d\n", loops, stores, initialized.size(), values_are_zero);
    if (loops || stores != count || !bounded || !values_are_zero || !logger.getAllMessages().empty()) return 1;
    puts("PASS every vertex output register initialized once without dynamic array indexing");
}
'''
(out / 'probe.cpp').write_text(cpp)
if os.name == 'nt' and Path('C:/msys64/ucrt64/bin').is_dir():
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler, 'A C++20 host compiler is required'
sources = ['SpvBuilder.cpp', 'SpvPostProcess.cpp', 'Logger.cpp', 'InReadableOrder.cpp']
subprocess.run([compiler, '-std=c++20', '-O0', '-g0', '-ffunction-sections', '-fdata-sections',
    '-I'+str(glslang), str(out / 'probe.cpp'),
    *[str(glslang / 'SPIRV' / source) for source in sources],
    '-Wl,--gc-sections', '-o', str(out / 'probe.exe')], check=True)
run = subprocess.run([str(out / 'probe.exe')], capture_output=True, text=True, timeout=10)
print(run.stdout + run.stderr)
(out / ('red.log' if run.returncode else 'green.log')).write_text(run.stdout + run.stderr)
raise SystemExit(run.returncode)
