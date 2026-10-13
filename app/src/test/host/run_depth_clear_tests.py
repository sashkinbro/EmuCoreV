#!/usr/bin/env python3
"""Check the actual Vulkan depth-clear expression against its legal range."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[4]
renderer = root / 'app/src/main/cpp/vita3k/vita3k/renderer'
source_path = 'app/src/main/cpp/vita3k/vita3k/renderer/src/vulkan/context.cpp'
source = (subprocess.check_output(['git', 'show', 'HEAD:' + source_path], cwd=root, text=True)
          if '--baseline' in sys.argv else (root / source_path).read_text())
clear = source[source.index('const vk::ClearDepthStencilValue ds_clear{'):]
expression = re.search(r'\.depth\s*=\s*(.*?),\s*\n', clear).group(1)
out = root / 'app/build/depth-clear-probe'
out.mkdir(parents=True, exist_ok=True)
header = renderer / 'include/renderer/vulkan/depth_clear.h'
cpp = '#include <cmath>\n#include <cstdio>\n#include <limits>\n'
if header.exists():
    cpp += '#include <renderer/vulkan/depth_clear.h>\n'
cpp += 'using namespace std;\n'
cpp += 'namespace renderer::vulkan { float clear(float value) { struct {struct {float background_depth;} depth_stencil_surface;} record{{value}}; return ' + expression + '; }}\n'
cpp += r'''
int main() {
    unsigned failures=0, checks=0;
    const float inf=std::numeric_limits<float>::infinity(), nan=std::numeric_limits<float>::quiet_NaN();
    const float inputs[]={0, 0.25f, 0.5f, 1, -1.0e-38f, -1, 2, -inf, inf, nan};
    const float expected[]={0, 0.25f, 0.5f, 1, 0, 0, 1, 0, 1, 0};
    for(unsigned i=0;i<10;++i) {
        ++checks;const float actual=renderer::vulkan::clear(inputs[i]);
        if(!std::isfinite(actual)||actual<0||actual>1||actual!=expected[i]) {
            ++failures;std::printf("FAIL depth=%g clear=%g expected=%g\n",inputs[i],actual,expected[i]);
        }
    }
    printf("%s Vulkan depth clear: %u checks, %u failures\n",failures?"FAIL":"PASS",checks,failures);
    return !!failures;
}
'''
(out / 'probe.cpp').write_text(cpp)
if os.name == 'nt' and Path('C:/msys64/ucrt64/bin').is_dir():
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler, 'A C++20 compiler is required'
subprocess.run([compiler, '-std=c++20', '-O0', '-I'+str(renderer / 'include'),
    str(out / 'probe.cpp'), '-o', str(out / 'probe.exe')], check=True)
run = subprocess.run([str(out / 'probe.exe')], capture_output=True, text=True, timeout=10)
print(run.stdout + run.stderr)
(out / ('red.log' if run.returncode else 'green.log')).write_text(run.stdout + run.stderr)
raise SystemExit(run.returncode)
