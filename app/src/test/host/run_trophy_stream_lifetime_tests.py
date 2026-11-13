#!/usr/bin/env python3
"""Exercise actual Context callback/reuse bodies with file-backed host IO adapters.

Only the IOState/read/seek boundary is adapted to stdio. Context, TRPFile,
constructor, binder (when present), and free-slot reuse loop are production code.
Native companion tests exercise the emulator's actual IOState/FileStats path.
"""
from pathlib import Path
import os
import shutil
import subprocess

root = next(p for p in Path(__file__).resolve().parents if (p / 'app/src/main/cpp/vita3k/vita3k').is_dir())
base = root / 'app/src/main/cpp/vita3k/vita3k'
out = root / 'tools/system-services/trophy-stream-lifetime-host'
out.mkdir(parents=True, exist_ok=True)
source = (base / 'np/src/trophy/context.cpp').read_text(encoding='utf-8')
constructor = source[source.index('Context::Context('):source.index('#define SET_TROPHY_BIT')]
reuse = source[source.index('    for (auto &context : np.trophy_state.contexts)', source.index('// Check for free slot first')):source.index('    if (!new_context)', source.index('// Check for free slot first'))]
cpp = r'''
#include <np/trophy/context.h>
#include <cstdio>
#include <map>
#include <utility>
#include <vector>
constexpr int SCE_SEEK_SET = SEEK_SET;
struct IOState { std::map<int, FILE*> files; };
int read_file(void *out, IOState &io, int fd, uint32_t amount, const char*) {
    auto it = io.files.find(fd);
    return it == io.files.end() ? -1 : int(fread(out, 1, amount, it->second));
}
int64_t seek_file(int fd, uint32_t offset, int whence, IOState &io, const char*) {
    auto it = io.files.find(fd);
    if (it == io.files.end() || fseek(it->second, offset, whence)) return -1;
    return ftell(it->second);
}
namespace np::trophy {
''' + constructor + r'''
}
using np::trophy::Context;
Context *reuse_slot(std::vector<Context> &contexts, IOState *io, int trophy_file) {
    struct { struct { std::vector<Context> &contexts; } trophy_state; } np{{contexts}};
    np::CommunicationID id{};
    const auto *custom_comm = &id;
    std::string trophy_progress_save_file;
    Context *new_context = nullptr;
''' + reuse + r'''
    return new_context;
}
int main() {
    int failures = 0, checks = 0;
    auto check = [&](bool ok, const char *name) {
        ++checks; failures += !ok;
        printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    };
    FILE *a = tmpfile(), *b = tmpfile();
    if (!a || !b) return 2;
    fwrite("ABCDEFGH", 1, 8, a); fflush(a); rewind(a);
    fwrite("abcdefgh", 1, 8, b); fflush(b); rewind(b);
    IOState first, second;
    first.files[11] = a; first.files[22] = a;
    second.files[11] = b; second.files[22] = b;
    np::CommunicationID id{};
    auto expect_bytes = [&](Context &context, char expected, const char *label) {
        char byte = '?';
        check(context.trophy_file.seek_func(2), label);
        check(context.trophy_file.read_func(&byte, 1) && byte == expected, label);
    };
    {
        Context original(id, &first, 11, "");
        Context moved(std::move(original));
        // Deterministically expose a callback tied to the old address without UB.
        original.io = &second; original.trophy_file_stream = 22;
        expect_bytes(moved, 'C', "moved context keeps original stream binding");
        check(ftell(a) == 3 && ftell(b) == 0, "move does not seek/read replacement stream");
    }
    rewind(a); rewind(b);
    {
        Context original(id, &first, 11, "");
        Context copied(original);
        original.io = &second; original.trophy_file_stream = 22;
        expect_bytes(copied, 'C', "copied context keeps original stream binding");
        check(ftell(a) == 3 && ftell(b) == 0, "copy does not seek/read replacement stream");
    }
    rewind(a); rewind(b);
    {
        std::vector<Context> contexts;
        contexts.emplace_back(id, &first, 11, "");
        contexts[0].valid = false;
        auto *reused = reuse_slot(contexts, &second, 22);
        check(reused == &contexts[0] && reused->valid, "actual free-slot branch reuses context");
        check(reused->io == &second && reused->trophy_file_stream == 22, "reuse updates IO state and descriptor");
        expect_bytes(*reused, 'c', "reused context reads replacement file");
        check(ftell(a) == 0 && ftell(b) == 3, "reuse leaves old file untouched");
        Context moved(std::move(*reused));
        reused->io = &first; reused->trophy_file_stream = 11;
        expect_bytes(moved, 'c', "reused then moved context retains replacement stream");
    }
    fclose(a); fclose(b);
    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures != 0;
}
'''
(out / 'probe.cpp').write_text(cpp, encoding='utf-8')
compiler = os.environ.get('CXX') or shutil.which('g++') or 'C:/msys64/ucrt64/bin/g++.exe'
env = os.environ.copy()
env['PATH'] = str(Path(compiler).parent) + os.pathsep + env['PATH']
includes = [base / 'np/include', base / 'mem/include', base / 'util/include', root / 'app/src/main/cpp/vita3k/external/boost', root / 'app/src/main/cpp/vita3k/external/fmt/include']
subprocess.run([compiler, '-std=c++20', '-O0', '-D_GLIBCXX_DEBUG', *['-I' + str(i) for i in includes], str(out / 'probe.cpp'), '-o', str(out / 'probe.exe')], env=env, check=True)
result = subprocess.run([str(out / 'probe.exe')], env=env, capture_output=True, text=True, timeout=10)
print(result.stdout + result.stderr, end='')
(out / 'result.log').write_text(result.stdout + result.stderr, encoding='utf-8')
raise SystemExit(result.returncode)
