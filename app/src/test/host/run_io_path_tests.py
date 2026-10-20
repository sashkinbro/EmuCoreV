#!/usr/bin/env python3
"""Exercise the production snapshot path reader with real BufferReader bounds."""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
native = root / 'app/src/main/cpp'
source = (native / 'emucorev/src/savestate/savestate.cpp').read_text(encoding='utf-8')
reader = source[source.index('bool read_bounded_io_path('):source.index('bool valid_io_open_mode(')]
out = root / 'app/build/io-path-probe'
out.mkdir(parents=True, exist_ok=True)
cpp = r'''
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <emucorev/savestate/state_io.h>
namespace fs = std::filesystem;
using namespace emucorev::savestate;
''' + reader + r'''
int main() {
    int failures = 0;
    auto check = [&](bool ok, const char *name) {
        printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        failures += !ok;
    };
    auto accepts = [](const std::string &path) {
        BufferWriter writer;
        writer.str(path);
        BufferReader input(writer.data().data(), writer.size());
        std::string result;
        return read_bounded_io_path(input, result) && result == path;
    };
    check(accepts("ux0:/app/PCSE00235/./common.lnk"), "DOA5 live current-directory file descriptor");
    check(accepts("ux0:/app/PCSE00235/./data/./file.bin"), "multiple current-directory components");
    check(accepts("ux0:/app/PCSE00235/."), "current-directory directory descriptor");
    check(accepts("ux0:\\app\\PCSE00235\\.\\common.lnk"), "backslash current-directory component");
    check(accepts("ux0:/app/PCSE00235/common.lnk"), "ordinary file descriptor");
    check(accepts("ux0:/app/PCSE00235/.hidden/file..bin"), "dots within names are preserved");
    check(!accepts("ux0:/app/PCSE00235/../other/file"), "parent traversal remains rejected");
    check(!accepts("ux0:\\app\\PCSE00235\\..\\other"), "backslash parent traversal remains rejected");
    check(!accepts("ux0:/app/PCSE00235/./../other"), "dot cannot hide parent traversal");
    check(!accepts(""), "empty path rejected");
    check(!accepts(std::string(4097, 'a')), "oversized path rejected");
    check(!accepts(std::string("ux0:/file\0hidden", 16)), "embedded null rejected");
    BufferWriter truncated;
    truncated.u32(100);
    truncated.u8('x');
    BufferReader input(truncated.data().data(), truncated.size());
    std::string result;
    check(!read_bounded_io_path(input, result), "truncated payload rejected");
    return failures ? 1 : 0;
}
'''
(out / 'probe.cpp').write_text(cpp, encoding='utf-8')
if os.name == 'nt':
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler, 'host C++ compiler required'
subprocess.run([compiler, '-std=c++20', '-O0', '-I', str(native / 'emucorev/include'),
                str(out / 'probe.cpp'), '-o', str(out / 'probe.exe')], check=True)
result = subprocess.run([str(out / 'probe.exe')], capture_output=True, text=True)
print(result.stdout + result.stderr, end='')
(out / ('red.log' if result.returncode else 'green.log')).write_text(result.stdout + result.stderr)
raise SystemExit(result.returncode)
