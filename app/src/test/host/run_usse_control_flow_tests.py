#!/usr/bin/env python3
"""Compile the real USSE analyzer and differential CFG regression tests on host."""
from pathlib import Path
import argparse
import json
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
arguments = argparse.ArgumentParser()
arguments.add_argument('--fixture', type=Path, help='Optional local GXP; never copied into tracked tests')
options = arguments.parse_args()
out = root / 'app/build/usse-control-flow-probe'
out.mkdir(parents=True, exist_ok=True)
vendor = root / 'app/src/main/cpp/vita3k'
core = vendor / 'vita3k'
external = vendor / 'external'
if os.name == 'nt' and Path('C:/msys64/ucrt64/bin').is_dir():
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler, 'A C++20 host compiler is required'
test = root / 'app/src/test/core-api/usse_control_flow_tests.inc'
probe = ('#include <gtest/gtest.h>\n#include <shader/gxp_parser.h>\n'
    '// Unused uniform-metadata entry point in the analyzer TU; fail if called.\n'
    'namespace shader { usse::ProgramInput get_program_input(const SceGxmProgram &) { std::abort(); } }\n'
    '#include "' + test.as_posix() + '"\n')
if options.fixture:
    fixture_path = json.dumps(options.fixture.resolve().as_posix())
    probe += r'''
#include <fstream>
TEST(USSEControlFlow, LocalGxpFixtureAnalyzesWithoutLeakingBranches) {
    std::ifstream file(FIXTURE_PATH, std::ios::binary);
    const std::vector<char> bytes{std::istreambuf_iterator<char>(file), {}};
    ASSERT_GE(bytes.size(), sizeof(SceGxmProgram));
    SceGxmProgram program{};
    std::memcpy(&program, bytes.data(), sizeof(program));
    const size_t start = offsetof(SceGxmProgram, primary_program_offset) + program.primary_program_offset;
    ASSERT_GT(program.primary_program_instr_count, 0u);
    ASSERT_LE(start, bytes.size());
    ASSERT_LE(program.primary_program_instr_count, (bytes.size() - start) / sizeof(uint64_t));
    std::vector<uint64_t> instructions(program.primary_program_instr_count);
    std::memcpy(instructions.data(), bytes.data() + start, instructions.size() * sizeof(uint64_t));
    USSEBlockNode root(nullptr, 0);
    analyze(root, static_cast<uint32_t>(instructions.size() - 1),
        [&](uint32_t pc) { return instructions.at(pc); });
    for (uint8_t values = 0; values < 16; ++values) {
        std::vector<uint32_t> trace;
        cfg_tree_trace(root, instructions, values, trace);
    }
}
'''.replace('FIXTURE_PATH', fixture_path)
(out / 'probe.cpp').write_text(probe)
includes = [core / (part + '/include') for part in ('shader', 'gxm', 'util')]
includes += [external / 'boost', external / 'fmt/include', external / 'spdlog/include',
             external / 'googletest/googletest/include', external / 'googletest/googletest']
cmd = [compiler, '-std=c++20', '-O0', '-g0', '-pthread', '-ffunction-sections', '-fdata-sections',
       '-DFMT_HEADER_ONLY', '-DSPDLOG_FMT_EXTERNAL', '-DBOOST_ALL_NO_LIB']
cmd += ['-I' + str(path) for path in includes]
cmd += [str(out / 'probe.cpp'), str(core / 'shader/src/usse_program_analyzer.cpp'),
        str(external / 'googletest/googletest/src/gtest-all.cc'),
        str(external / 'googletest/googletest/src/gtest_main.cc'),
        '-Wl,--gc-sections', '-o', str(out / 'probe.exe')]
subprocess.run(cmd, check=True)
names = ['PredicatedFallthroughBeforeIfElsePreservesBothPaths', 'EveryFallthroughPredicateConsumesTheBranch',
         'FallthroughAtProgramEndDoesNotReadBeyondProgram', 'OrdinaryNestedConditionalBranchesKeepTheirPaths']
if options.fixture:
    names.append('LocalGxpFixtureAnalyzesWithoutLeakingBranches')
failed = 0
logs = []
for name in names:
    run = subprocess.run([str(out / 'probe.exe'), '--gtest_filter=USSEControlFlow.' + name],
                         capture_output=True, text=True, timeout=10)
    logs.append(run.stdout + run.stderr)
    print(logs[-1], end='')
    failed += bool(run.returncode)
(out / ('red.log' if failed else 'green.log')).write_text('\n'.join(logs))
raise SystemExit(bool(failed))
