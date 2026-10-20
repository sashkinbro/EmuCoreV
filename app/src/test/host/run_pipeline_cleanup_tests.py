#!/usr/bin/env python3
"""Exercise the production pipeline teardown with a recording Vulkan seam."""
from pathlib import Path
import os
import re
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
source = (root / 'app/src/main/cpp/vita3k/vita3k/renderer/src/vulkan/pipeline_cache.cpp').read_text(encoding='utf-8')
start = source.index('void PipelineCache::cleanup() {')
end = source.index('    {\n        std::lock_guard<std::mutex> guard(shaders_mutex);', start)
body = source[start:end].replace('void PipelineCache::cleanup()', 'void cleanup()') + '}\n'
constants = '\n'.join(re.findall(r'^const vk::Pipeline kPipeline\w+ = .*?;$', source, re.M))
cpp = r'''
#include <bit>
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>
namespace vk { struct PipelineTag {}; using Pipeline = PipelineTag*; }
''' + constants + r'''
struct Probe {
    bool use_async_compilation = true;
    bool joined = false;
    std::map<uint64_t, vk::Pipeline> pipelines;
    struct Device {
        bool* joined;
        bool destroyed_before_join = false;
        std::vector<uint64_t> destroyed;
        void destroy(vk::Pipeline pipeline) {
            destroyed_before_join |= !*joined;
            destroyed.push_back(std::bit_cast<uint64_t>(pipeline));
        }
    };
    struct State { Device device; } state{{&joined}};
    void set_async_compilation(bool enable) {
        use_async_compilation = enable;
        joined = true;
        // A pending worker finishes before cache destruction starts.
        pipelines[4] = std::bit_cast<vk::Pipeline>(uint64_t(44));
    }
''' + body + r'''
};
int main() {
    Probe p;
    p.pipelines = {{0, nullptr}, {1, std::bit_cast<vk::Pipeline>(uint64_t(11))},
        {2, std::bit_cast<vk::Pipeline>(~0ULL)}, {3, std::bit_cast<vk::Pipeline>(~1ULL)}};
    p.cleanup();
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        printf("%s %s\n", ok ? "PASS" : "FAIL", name); failures += !ok;
    };
    check(p.state.device.destroyed == std::vector<uint64_t>({11, 44}),
        "only real pipelines reach Vulkan destruction; cache markers never do");
    check(!p.state.device.destroyed_before_join, "compiler workers finish before handles are destroyed");
    check(p.pipelines.empty(), "all cache entries are cleared");
    const auto destroyed_once = p.state.device.destroyed;
    p.cleanup();
    check(p.state.device.destroyed == destroyed_once, "repeated cleanup cannot destroy handles twice");
    return failures ? 1 : 0;
}
'''
out = root / 'app/build/pipeline-cleanup-probe'
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
