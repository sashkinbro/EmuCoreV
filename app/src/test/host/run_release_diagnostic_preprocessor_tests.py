#!/usr/bin/env python3
"""Check production release/debug diagnostic guards with the C++ preprocessor.

Includes are omitted and LOG macros are stubbed. This checks conditional source
selection, not C++ types, linkage, generated machine code, or device performance.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
core = 'app/src/main/cpp/vita3k/vita3k/'
if os.name == 'nt' and Path('C:/msys64/ucrt64/bin').is_dir():
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
compiler = os.environ.get('CXX') or shutil.which('g++') or shutil.which('clang++')
assert compiler, 'C++ preprocessor required'

# Every listed diagnostic must disappear in release and remain in debug.
cases = {
    'app/src/main/cpp/discord_bridge.cpp': ['__android_log_print', 'AddLogCallback'],
    core + 'android/jni/native_bootstrap.cpp': ['setenv("TU_DEBUG"'],
    core + 'interface.cpp': ['server_open(emuenv)', 'wait_for_debugger = true'],
    core + 'app/src/app_init.cpp': ['server_close(emuenv)'],
    'app/src/main/cpp/emucorev/src/savestate/savestate.cpp': ['const uint32_t sp = read_sp(*thread->cpu);'],
    core + 'kernel/src/load_self.cpp': ['if (kernel.debugger.dump_elfs)', 'dump_elf.data()'],
    core + 'kernel/src/thread.cpp': ['is_probe(', 'probe_step_mutex', 'if (kernel.debugger.wait_for_debugger)'],
    core + 'android/jni/native_config.cpp': ['debugger.dump_elfs =', 'debugger.log_imports =', 'debugger.log_exports ='],
    core + 'renderer/src/vulkan/renderer.cpp': ['VITA3K_SYNC_VALIDATION', 'debug_util_callback', 'debug_report_callback', 'vk::enumerateInstanceLayerProperties()'],
    core + 'display/src/display.cpp': ['log_thread_hang_dump()', 'last_dumped_setframe', 'never_flip_redumped', 'next_forced_dump_vblank'],
    core + 'shader/src/spirv_recompiler.cpp': ['spirv_disasm_print(spirv, &spirv_dump);', 'std::ofstream spirv_dump', 'dumper("dsm"', 'struct DisassemblyScope'],
}
checks = 0
for relative, diagnostics in cases.items():
    source = (root / relative).read_text()
    source = re.sub(r'^\s*#\s*include\b[^\n]*', '', source, flags=re.M)
    # Model the production LOG macro contract so diagnostic strings cannot
    # accidentally satisfy presence checks after their arguments compile out.
    prelude = '\n'.join(f'#define LOG_{level}{suffix}(...) ((void)0)'
                        for level in ('TRACE', 'DEBUG', 'INFO', 'WARN', 'ERROR', 'CRITICAL')
                        for suffix in ('', '_IF', '_ONCE')) + '\n'
    outputs = {}
    for mode in ('release', 'debug'):
        result = subprocess.run([compiler, '-E', '-P', '-x', 'c++', '-D__ANDROID__',
                                 *(['-DNDEBUG'] if mode == 'release' else []), '-'],
                                input=prelude + source, text=True, capture_output=True, check=True)
        outputs[mode] = result.stdout
    for token in diagnostics:
        assert token not in outputs['release'], f'release retains {relative}: {token}'
        assert token in outputs['debug'], f'debug lost {relative}: {token}'
        checks += 2
    if relative.endswith('display/src/display.cpp'):
        # Keep both functional breakers and the measurements deciding when they
        # may mutate guest state. Logging cleanup must not disable recovery.
        for token in ('try_break_provable_evf_cycle()', 'try_break_frame_sync_deadlock(nudged_this_stall)',
                      'last_world_stop_epoch_ms.load', 'last_mem_transition_epoch_ms.load',
                      'renderer_idle_vblanks', 'guest_idle_vblanks', 'last_break_vblanks = vblanks'):
            assert token in outputs['release'], f'release lost recovery: {token}'
            checks += 1
    if relative.endswith('renderer/src/vulkan/renderer.cpp'):
        assert 'constexpr bool use_validation_layer = false;' in outputs['release']
        assert 'config.validation_layer;' in outputs['debug']
        checks += 2
    print(f'PASS {relative}')

# Compile and execute the actual disassembly macro in release. Neither enabled()
# nor formatting may inspect TLS or evaluate arguments in this mode.
header = (root / (core + 'shader/include/shader/usse_disasm.h')).read_text()
enabled = header[header.index('inline bool enabled()'):header.index('//\n// Disasm helpers')]
macros = header[header.index('// TODO: make LOG_RAW'):]
probe = '#include <cstdio>\nnamespace shader::usse::disasm {\n' + enabled + '}\n' + macros + r'''
int main(){int effects=0;LOG_DISASM("{}",++effects);
 return effects || shader::usse::disasm::enabled();}
'''
out = root / 'app/build/release-diagnostics-probe'
out.mkdir(parents=True, exist_ok=True)
(out / 'disasm.cpp').write_text(probe)
subprocess.run([compiler, '-DNDEBUG', str(out / 'disasm.cpp'), '-o', str(out / 'disasm.exe')], check=True)
subprocess.run([str(out / 'disasm.exe')], check=True)
checks += 1
print(f'{checks} release/debug preprocessor checks passed')
