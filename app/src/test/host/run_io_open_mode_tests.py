#!/usr/bin/env python3
"""Check save-state descriptor validation against the core's actual file modes."""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
native = root / 'app/src/main/cpp'
source = (native / 'emucorev/src/savestate/savestate.cpp').read_text(encoding='utf-8')
validator = source[source.index('bool valid_io_open_mode('):source.index('bool parse_io_state(')]
types = (native / 'vita3k/vita3k/io/include/io/types.h').read_text(encoding='utf-8')
flags = types[types.index('typedef enum SceIoMode'):types.index('typedef enum SceIoAccessMode')]
filesystem = (native / 'vita3k/vita3k/io/src/filesystem.cpp').read_text(encoding='utf-8')
translation = filesystem[filesystem.index('const char *translate_open_mode'):filesystem.rindex('#endif')]
out = root / 'app/build/io-open-mode-probe'
out.mkdir(parents=True, exist_ok=True)
cpp = '#include <cstdint>\n#include <cstdio>\n#include <cstring>\n' + flags + translation + validator + r'''
int main() {
    int failures = 0;
    auto check = [&](bool ok, const char *name) {
        printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        failures += !ok;
    };
    check(std::strcmp(translate_open_mode(SCE_O_CREAT), "rb") == 0,
        "Rayman CREAT-only descriptor is opened for reading by the core");
    check(valid_io_open_mode(SCE_O_CREAT), "Rayman CREAT-only descriptor can be saved");
    check(valid_io_open_mode(0), "implicit read-only mode can be restored after stripping CREAT");
    check(valid_io_open_mode(SCE_O_RDONLY), "explicit read-only mode accepted");
    check(valid_io_open_mode(SCE_O_RDWR | SCE_O_CREAT | SCE_O_TRUNC), "read/write creation mode accepted");
    check(valid_io_open_mode(SCE_O_WRONLY | SCE_O_APPEND), "append writer accepted");
    check(!valid_io_open_mode(SCE_O_DIROPEN), "internal directory mode remains rejected for files");
    check(!valid_io_open_mode(0x40), "unknown flags remain rejected");
    check(!valid_io_open_mode(-1), "negative mode remains rejected");
    return failures ? 1 : 0;
}
'''
(out / 'probe.cpp').write_text(cpp, encoding='utf-8')
if os.name == 'nt':
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler, 'host C++ compiler required'
subprocess.run([compiler, '-std=c++20', '-O0', str(out / 'probe.cpp'), '-o', str(out / 'probe.exe')], check=True)
result = subprocess.run([str(out / 'probe.exe')], capture_output=True, text=True)
print(result.stdout + result.stderr, end='')
(out / ('red.log' if result.returncode else 'green.log')).write_text(result.stdout + result.stderr)
raise SystemExit(result.returncode)
