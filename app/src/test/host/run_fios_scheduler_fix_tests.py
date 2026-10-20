#!/usr/bin/env python3
"""Compile the real guest patcher with bounded memory and cache-invalidation seams."""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
path = root / 'app/src/main/cpp/vita3k/vita3k/modules/guest_code_fixes.cpp'
source = path.read_text(encoding='utf-8') if path.exists() else ''
source = '\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
if not source:
    source = 'void apply_guest_code_fixes(EmuEnvState&, const SceKernelModuleInfo&) {}'
cpp = r'''
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>
using Address = uint32_t;
struct Pointer { Address value = 0; Address address() const { return value; } };
struct SceKernelSegmentInfo { uint32_t perms = 0; Pointer vaddr; uint32_t memsz = 0; };
struct SceKernelModuleInfo { char module_name[28]{}; std::array<SceKernelSegmentInfo, 4> segments{}; };
struct MemState {
    std::vector<uint8_t> bytes = std::vector<uint8_t>(4096);
    bool readable = true, partial_write_once = false, fail_readback_once = false;
    unsigned writes = 0;
};
struct Kernel { std::vector<std::pair<Address, uint32_t>> invalidations;
    void invalidate_jit_cache(Address a, uint32_t n) { invalidations.emplace_back(a, n); } };
struct EmuEnvState { MemState mem; Kernel kernel; };
bool debug_safe_copy_guest(MemState& m, Address a, void* p, uint32_t n) {
    if (!m.readable || !a || uint64_t(a) + n > m.bytes.size()) return false;
    if (m.writes && m.fail_readback_once) { m.fail_readback_once = false; return false; }
    memcpy(p, m.bytes.data() + a, n); return true;
}
bool debug_safe_write_guest(MemState& m, Address a, const void* p, uint32_t n) {
    if (!a || uint64_t(a) + n > m.bytes.size()) return false;
    ++m.writes;
    if (m.partial_write_once) {
        m.partial_write_once = false;
        memcpy(m.bytes.data() + a, p, n/2); return false;
    }
    memcpy(m.bytes.data() + a, p, n); return true;
}
#define LOG_INFO(...) ((void)0)
#define LOG_WARN(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
''' + source + r'''
const std::vector<uint8_t> original = {
    0x41,0xf2,0x7c,0x00,0x58,0xf8,0x00,0x20,0x40,0x46,0x39,0x1c,0x01,0xf0,0x23,0xf8,
    0x58,0xf8,0x04,0x00,0x01,0x30,0x48,0xf8,0x04,0x00,0x02,0xe0,0x28,0x1c,0x02,0xf0,0x45,0xf8,
    0x1e,0x20,0xc8,0xf2,0x82,0x00};
const std::vector<uint8_t> expected = {
    0x08,0xeb,0x04,0x06,0xb2,0x68,0x40,0x46,0x39,0x1c,0x00,0xbf,0x01,0xf0,0x23,0xf8,
    0x30,0x68,0x10,0xb1,0x01,0x30,0x30,0x60,0x00,0xbf,0x02,0xe0,0x28,0x1c,0x02,0xf0,0x45,0xf8,
    0x1e,0x20,0xc8,0xf2,0x82,0x00};
int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* name) { printf("%s %s\n", ok ? "PASS" : "FAIL", name); failures += !ok; };
    auto fixture = [&](const char* name, Address site = 160) {
        EmuEnvState e;
        std::copy(original.begin(), original.end(), e.mem.bytes.begin() + site);
        SceKernelModuleInfo m;
        std::strcpy(m.module_name, name);
        m.segments[0] = {5, {128}, 256};
        return std::pair(e, m);
    };
    for (const auto name : {"SceLibFios2", "SceLibFios3"}) {
        auto [e, m] = fixture(name);
        auto before = e.mem.bytes;
        apply_guest_code_fixes(e, m);
        check(std::equal(expected.begin(), expected.end(), e.mem.bytes.begin() + 160), name);
        check(e.kernel.invalidations == std::vector<std::pair<Address,uint32_t>>{{160,40}}, "invalidate the patched guest instruction range");
        check(std::equal(before.begin(), before.begin()+160, e.mem.bytes.begin()) &&
            std::equal(before.begin()+200, before.end(), e.mem.bytes.begin()+200), "preserve surrounding guest bytes");
        auto patched = e.mem.bytes;
        apply_guest_code_fixes(e, m);
        check(patched == e.mem.bytes && e.kernel.invalidations.size() == 1, "applying twice is harmless");
    }
    for (int scenario = 0; scenario < 7; ++scenario) {
        auto [e, m] = fixture("SceLibFios3", scenario == 5 ? 161 : 160);
        if (scenario == 0) std::strcpy(m.module_name, "UnrelatedModule");
        if (scenario == 1) e.mem.bytes[160] ^= 1;
        if (scenario == 2) std::copy(original.begin(), original.end(), e.mem.bytes.begin()+224);
        if (scenario == 3) m.segments[0].perms = 6;
        if (scenario == 4) { m.segments[0].memsz = 66; } // match crosses the segment boundary
        if (scenario == 6) e.mem.readable = false;
        const auto before = e.mem.bytes;
        apply_guest_code_fixes(e, m);
        check(e.mem.bytes == before && e.kernel.invalidations.empty(), "reject unknown, changed, ambiguous, non-code, truncated, unaligned or unreadable code");
    }
    {
        auto [e,m] = fixture("SceLibFios3");
        m.segments[1] = {5,{512},128};
        std::copy(original.begin(),original.end(),e.mem.bytes.begin()+544);
        auto before=e.mem.bytes;
        apply_guest_code_fixes(e,m);
        check(e.mem.bytes==before && e.kernel.invalidations.empty(), "uniqueness is checked across all executable segments");
    }
    for (int failure = 0; failure < 2; ++failure) {
        auto [e,m] = fixture("SceLibFios3");
        e.mem.partial_write_once = failure == 0;
        e.mem.fail_readback_once = failure == 1;
        auto before = e.mem.bytes;
        apply_guest_code_fixes(e,m);
        check(e.mem.bytes == before && e.mem.writes == 2, "restore original instructions after partial write or failed verification");
        check(e.kernel.invalidations == std::vector<std::pair<Address,uint32_t>>{{160,40}}, "invalidate JIT after rollback too");
    }
    return failures ? 1 : 0;
}
'''
out = root / 'app/build/fios-scheduler-probe'
out.mkdir(parents=True, exist_ok=True)
(out / 'probe.cpp').write_text(cpp, encoding='utf-8')
if os.name == 'nt':
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler, 'host C++ compiler required'
subprocess.run([compiler, '-std=c++20', str(out/'probe.cpp'), '-o', str(out/'probe.exe')], check=True)
result = subprocess.run([str(out/'probe.exe')], capture_output=True, text=True)
print(result.stdout + result.stderr, end='')
(out/('red.log' if result.returncode else 'green.log')).write_text(result.stdout + result.stderr)
raise SystemExit(result.returncode)
