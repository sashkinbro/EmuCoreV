#!/usr/bin/env python3
"""Host tests for the GXM -> Vulkan sampler state decisions (renderer/sampler_state.h): address modes (repeat, mirror,
clamp, mirror-clamp with and without VK_KHR_sampler_mirror_clamp_to_edge, border modes), LOD bias range and device
limit, anisotropy planning. Compiles the real header, no stubs."""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
core = root / 'app/src/main/cpp/emucorev/ECV/ECV'
out = root / 'app/build/sampler-state-probe'
out.mkdir(parents=True, exist_ok=True)

cpp = r'''
#include <renderer/sampler_state.h>
#include <cmath>
#include <cstdio>
using namespace renderer;

static int failures = 0, checks = 0;
static void check(bool ok, const char *what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); } }
static void expect(uint32_t mode, bool mirror_clamp, AddressKind kind, bool approximate, const char *what) {
    const AddressTranslation t = translate_address(mode, mirror_clamp);
    ++checks;
    if (t.kind != kind || t.approximate != approximate) {
        ++failures;
        std::printf("FAIL %s: kind %d approximate %d, expected %d %d\n", what, (int)t.kind, (int)t.approximate, (int)kind, (int)approximate);
    }
}

int main() {
    // ---- address modes, with VK_KHR_sampler_mirror_clamp_to_edge
    for (bool mc : { false, true }) {
        expect(SCE_GXM_TEXTURE_ADDR_REPEAT, mc, AddressKind::Repeat, false, "repeat");
        expect(SCE_GXM_TEXTURE_ADDR_MIRROR, mc, AddressKind::MirroredRepeat, false, "mirror");
        expect(SCE_GXM_TEXTURE_ADDR_CLAMP, mc, AddressKind::ClampToEdge, false, "clamp");
        expect(SCE_GXM_TEXTURE_ADDR_REPEAT_IGNORE_BORDER, mc, AddressKind::Repeat, false, "repeat ignore border");
        expect(SCE_GXM_TEXTURE_ADDR_CLAMP_IGNORE_BORDER, mc, AddressKind::ClampToEdge, false, "clamp ignore border");
        expect(SCE_GXM_TEXTURE_ADDR_CLAMP_FULL_BORDER, mc, AddressKind::ClampToBorder, false, "clamp full border");
        expect(SCE_GXM_TEXTURE_ADDR_CLAMP_HALF_BORDER, mc, AddressKind::ClampToBorder, true, "clamp half border (documented approximation)");
        expect(99, mc, AddressKind::ClampToEdge, true, "unknown mode");
    }
    expect(SCE_GXM_TEXTURE_ADDR_MIRROR_CLAMP, true, AddressKind::MirrorClampToEdge, false, "mirror clamp with the extension");
    expect(SCE_GXM_TEXTURE_ADDR_MIRROR_CLAMP, false, AddressKind::ClampToEdge, true, "mirror clamp without the extension falls back to clamp");

    // ---- LOD bias: 6 bit field, offset 31, 1/8 steps, clamped to the device limit
    check(translate_lod_bias(31, 16.0f) == 0.0f, "bias field 31 is 0");
    check(translate_lod_bias(0, 16.0f) == -3.875f, "smallest bias");
    check(translate_lod_bias(63, 16.0f) == 4.0f, "largest bias");
    check(translate_lod_bias(39, 16.0f) == 1.0f, "bias +1");
    check(translate_lod_bias(63, 2.0f) == 2.0f, "bias clamped to a low device limit");
    check(translate_lod_bias(0, 2.0f) == -2.0f, "negative bias clamped to a low device limit");
    check(translate_lod_bias(63, 0.0f) == 0.0f && translate_lod_bias(0, 0.0f) == 0.0f, "no bias allowed");

    // ---- anisotropy
    {
        SamplerSupport s; s.anisotropy = true; s.max_anisotropy = 16.0f;
        AnisotropyPlan p = plan_anisotropy(8, s, true, true, false);
        check(p.enable && p.max_anisotropy == 8.0f, "anisotropy 8");
        p = plan_anisotropy(16, s, true, false, false);
        check(p.enable && p.max_anisotropy == 16.0f, "anisotropy with one linear filter");
        p = plan_anisotropy(1, s, true, true, false);
        check(!p.enable, "host setting off");
        p = plan_anisotropy(8, s, false, false, false);
        check(!p.enable, "nearest filters (the texels may hold packed data)");
        p = plan_anisotropy(8, s, true, true, true);
        check(!p.enable, "never with unnormalized coordinates");
        s.max_anisotropy = 4.0f;
        p = plan_anisotropy(16, s, true, true, false);
        check(p.enable && p.max_anisotropy == 4.0f, "clamped to the device limit");
        s.anisotropy = false;
        p = plan_anisotropy(16, s, true, true, false);
        check(!p.enable && p.max_anisotropy == 1.0f, "device without samplerAnisotropy");
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
'''
(out / 'probe.cpp').write_text(cpp, encoding='utf-8')
if os.name == 'nt' and Path('C:/msys64/ucrt64/bin').is_dir():
    os.environ['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ['PATH']
compiler = os.environ.get('CXX') or shutil.which('g++')
assert compiler, 'A C++20 compiler is required'
subprocess.run([compiler, '-std=c++20', '-O0', '-Wall', '-Wextra',
    '-I' + str(core / 'renderer/include'), '-I' + str(core / 'gxm/include'), '-I' + str(core / 'util/include'),
    str(out / 'probe.cpp'), '-o', str(out / 'probe.exe')], check=True)
result = subprocess.run([str(out / 'probe.exe')], capture_output=True, text=True, timeout=30)
print(result.stdout + result.stderr)
(out / ('red.log' if result.returncode else 'green.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
raise SystemExit(result.returncode)
