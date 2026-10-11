#!/usr/bin/env python3
"""Run the production visibility range/reset block across repeated scenes."""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
source = (root / 'app/src/main/cpp/vita3k/vita3k/renderer/src/vulkan/context.cpp').read_text()
start = source.index('    struct VisibilityRange {', source.index('void VKContext::stop_recording('))
end = source.index('    // remember which part', start)
body = source[start:end]
out = root / 'app/build/visibility-query-reset-probe'
out.mkdir(parents=True, exist_ok=True)
cpp = r'''
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <utility>
#include <vector>
namespace vk { enum class QueryResultFlagBits { eWait }; }
// Bounds-check only the container seam. The production loop/reset is unchanged.
struct CheckedBits : std::vector<bool> {
    using std::vector<bool>::vector;
    reference operator[](size_t index) { return at(index); }
};
struct VisibilityBuffer {
    uint32_t size;
    int query_pool = 1, gpu_buffer = 2;
    uint32_t buffer_offset = 0;
    CheckedBits queries_used;
    explicit VisibilityBuffer(uint32_t count) : size(count), queries_used(count + 1, false) {}
};
struct Commands {
    struct Range { uint32_t offset, count; };
    std::vector<Range> resets, copies;
    void resetQueryPool(int, uint32_t offset, uint32_t count) { resets.push_back({offset, count}); }
    void copyQueryPoolResults(int, uint32_t offset, uint32_t count, int, uint32_t, size_t, vk::QueryResultFlagBits) {
        copies.push_back({offset, count});
    }
};
struct Context {
    VisibilityBuffer *current_visibility_buffer;
    int visibility_max_used_idx = -1;
    Commands prerender_cmd, render_cmd;
    void finish_queries() {
BODY
    }
};
int failures = 0, checks = 0;
void check(bool value, const char *label) {
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %s\n", label); }
}
void run_scenes(uint32_t count) {
    VisibilityBuffer buffer(count);
    Context context{&buffer};
    for (int scene = 0; scene != 3; ++scene) {
        // Two disjoint runs exercise both normal closure and the terminal sentinel.
        buffer.queries_used[0] = true;
        buffer.queries_used[count - 1] = true;
        context.visibility_max_used_idx = static_cast<int>(count - 1);
        context.prerender_cmd.resets.clear();
        context.render_cmd.copies.clear();
        try { context.finish_queries(); }
        catch (const std::out_of_range &) {
            check(false, "repeated scene range builder must not read beyond query storage");
            return;
        }
        check(context.visibility_max_used_idx == -1, "finished scene releases query high-water mark");
        check(buffer.queries_used.size() == count + 1, "reset retains terminal sentinel");
        bool cleared = true;
        for (bool value : buffer.queries_used) cleared &= !value;
        check(cleared, "reset clears all scene query usage including sentinel");
        const auto &copies = context.render_cmd.copies;
        const uint32_t expected_runs = count == 1 ? 1 : 2;
        check(copies.size() == expected_runs && context.prerender_cmd.resets.size() == expected_runs,
            "every used run is reset and copied once");
        if (!copies.empty()) {
            check(copies.back().offset == count - 1 && copies.back().count == 1,
                "final valid query closes at sentinel without copying it");
        }
    }
}
int main() {
    run_scenes(1);
    run_scenes(8);
    // No-query scene must not emit reset/copy work.
    VisibilityBuffer buffer(8);
    Context context{&buffer};
    context.finish_queries();
    check(context.render_cmd.copies.empty() && context.prerender_cmd.resets.empty(),
        "unused visibility buffer produces no GPU query work");
    std::printf("Visibility query resets: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
'''.replace('BODY', body)
(out / 'probe.cpp').write_text(cpp)
if os.name == 'nt' and Path('C:/msys64/ucrt64/bin').is_dir():
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
compiler = os.environ.get('CXX') or shutil.which('g++') or shutil.which('clang++')
assert compiler, 'A C++20 compiler is required'
exe = out / 'probe.exe'
subprocess.run([compiler, '-std=c++20', str(out / 'probe.cpp'), '-o', str(exe)], check=True)
result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
print(result.stdout + result.stderr)
(out / ('red.log' if result.returncode else 'green.log')).write_text(result.stdout + result.stderr)
raise SystemExit(result.returncode)
