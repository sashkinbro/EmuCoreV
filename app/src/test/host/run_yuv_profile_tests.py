#!/usr/bin/env python3
"""Host tests for the GXM YUV semantics (renderer/yuv.h): profile matrices with reference pixel values, texture
format decoding, plane layout, the CPU conversion that is also the reference of the compute shader, and the choice
of the conversion route from the probed capabilities. Compiles the real header, no stubs."""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]
core = root / 'app/src/main/cpp/emucorev/ECV/ECV'
out = root / 'app/build/yuv-profile-probe'
out.mkdir(parents=True, exist_ok=True)

cpp = r'''
#include <renderer/yuv.h>
#include <gxm/types.h>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace renderer::yuv;

static int failures = 0, checks = 0;
static void check(bool ok, const char *what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); } }
static void check_near(double a, double b, double tol, const char *what) {
    ++checks;
    if (std::fabs(a - b) > tol) { ++failures; std::printf("FAIL %s: %g vs %g (tol %g)\n", what, a, b, tol); }
}

static void px(uint8_t profile, int y, int u, int v, int er, int eg, int eb, int tol, const char *what) {
    uint8_t rgb[3];
    convert_pixel(profile, (uint8_t)y, (uint8_t)u, (uint8_t)v, rgb);
    ++checks;
    if (std::abs(rgb[0] - er) > tol || std::abs(rgb[1] - eg) > tol || std::abs(rgb[2] - eb) > tol) {
        ++failures;
        std::printf("FAIL %s: yuv(%d,%d,%d) -> (%d,%d,%d) expected (%d,%d,%d) +-%d\n", what, y, u, v, rgb[0], rgb[1], rgb[2], er, eg, eb, tol);
    }
}

int main() {
    // ---- matrix coefficients (ITU-R BT.601 / BT.709 definitions)
    {
        Matrix m = make_matrix(BT601_STANDARD);
        check_near(m.r_v, 1.402, 1e-6, "601 r_v"); check_near(m.b_u, 1.772, 1e-6, "601 b_u");
        check_near(m.g_u, -0.344136, 1e-5, "601 g_u"); check_near(m.g_v, -0.714136, 1e-5, "601 g_v");
        check_near(m.y_bias * 255, 16, 1e-4, "601 std y bias"); check_near(m.y_scale, 255.0 / 219.0, 1e-6, "601 std y scale");
        check_near(m.c_scale, 255.0 / 224.0, 1e-6, "601 std c scale");
        Matrix f = make_matrix(BT601_FULL_RANGE);
        check(f.y_bias == 0 && f.y_scale == 1 && f.c_scale == 1, "601 full range has no scaling");
        check_near(f.r_v, 1.402, 1e-6, "601 full uses the 601 coefficients");
        Matrix h = make_matrix(BT709_STANDARD);
        check_near(h.r_v, 1.5748, 1e-6, "709 r_v"); check_near(h.b_u, 1.8556, 1e-6, "709 b_u");
        check_near(h.g_u, -0.187324, 1e-5, "709 g_u"); check_near(h.g_v, -0.468124, 1e-5, "709 g_v");
        check_near(h.y_scale, 255.0 / 219.0, 1e-6, "709 std y scale");
        Matrix hf = make_matrix(BT709_FULL_RANGE);
        check(hf.y_bias == 0 && hf.y_scale == 1 && hf.c_scale == 1, "709 full range has no scaling");
        check_near(hf.b_u, 1.8556, 1e-6, "709 full uses the 709 coefficients");
    }

    // ---- reference pixels (published YCbCr values of the primaries and white / black / grey)
    px(BT601_STANDARD, 16, 128, 128, 0, 0, 0, 0, "601 std black");
    px(BT601_STANDARD, 235, 128, 128, 255, 255, 255, 0, "601 std white");
    px(BT601_STANDARD, 126, 128, 128, 128, 128, 128, 1, "601 std grey");
    px(BT601_STANDARD, 81, 90, 240, 255, 0, 0, 3, "601 std red");
    px(BT601_STANDARD, 145, 54, 34, 0, 255, 0, 3, "601 std green");
    px(BT601_STANDARD, 41, 240, 110, 0, 0, 255, 3, "601 std blue");
    px(BT601_STANDARD, 0, 128, 128, 0, 0, 0, 0, "601 std below black clamps");
    px(BT601_STANDARD, 255, 128, 128, 255, 255, 255, 0, "601 std above white clamps");
    px(BT601_FULL_RANGE, 0, 128, 128, 0, 0, 0, 0, "601 full black");
    px(BT601_FULL_RANGE, 255, 128, 128, 255, 255, 255, 0, "601 full white");
    px(BT601_FULL_RANGE, 76, 85, 255, 255, 0, 0, 3, "601 full red");
    px(BT601_FULL_RANGE, 150, 44, 21, 0, 255, 0, 3, "601 full green");
    px(BT601_FULL_RANGE, 29, 255, 107, 0, 0, 255, 3, "601 full blue");
    px(BT709_STANDARD, 16, 128, 128, 0, 0, 0, 0, "709 std black");
    px(BT709_STANDARD, 235, 128, 128, 255, 255, 255, 0, "709 std white");
    px(BT709_STANDARD, 63, 102, 240, 255, 0, 0, 3, "709 std red");
    px(BT709_STANDARD, 173, 42, 26, 0, 255, 0, 3, "709 std green");
    px(BT709_STANDARD, 32, 240, 118, 0, 0, 255, 3, "709 std blue");
    px(BT709_FULL_RANGE, 255, 128, 128, 255, 255, 255, 0, "709 full white");
    px(BT709_FULL_RANGE, 54, 99, 255, 255, 0, 0, 3, "709 full red");
    px(BT709_FULL_RANGE, 182, 30, 12, 0, 255, 0, 3, "709 full green");
    px(BT709_FULL_RANGE, 18, 255, 116, 0, 0, 255, 3, "709 full blue");
    // the same YCbCr is a different colour in each profile (profile selection matters)
    {
        uint8_t a[3], b[3], c[3], d[3];
        convert_pixel(BT601_STANDARD, 100, 80, 200, a); convert_pixel(BT709_STANDARD, 100, 80, 200, b);
        convert_pixel(BT601_FULL_RANGE, 100, 80, 200, c); convert_pixel(BT709_FULL_RANGE, 100, 80, 200, d);
        check(a[0] != b[0] || a[1] != b[1] || a[2] != b[2], "601 and 709 differ");
        check(a[0] != c[0] || a[1] != c[1] || a[2] != c[2], "standard and full range differ");
        check(c[0] != d[0] || c[1] != d[1] || c[2] != d[2], "601 full and 709 full differ");
    }
    // round trip over a colour cube, forward matrix straight from the BT definitions
    for (int p = 0; p < 4; p++) {
        const bool b709 = p >= 2, full = (p & 1) != 0;
        const double kr = b709 ? 0.2126 : 0.299, kb = b709 ? 0.0722 : 0.114, kg = 1 - kr - kb;
        int worst = 0;
        for (int r = 0; r <= 255; r += 51) for (int g = 0; g <= 255; g += 51) for (int b = 0; b <= 255; b += 51) {
            double y = kr * r + kg * g + kb * b, cb = (b - y) / (2 * (1 - kb)), cr = (r - y) / (2 * (1 - kr));
            double Y, U, V;
            if (full) { Y = y; U = cb + 128; V = cr + 128; }
            else { Y = 16 + y * 219 / 255; U = 128 + cb * 224 / 255; V = 128 + cr * 224 / 255; }
            uint8_t rgb[3];
            auto q = [](double v) { return (uint8_t)std::min(255.0, std::max(0.0, std::round(v))); };
            convert_pixel((uint8_t)p, q(Y), q(U), q(V), rgb);
            worst = std::max(worst, std::max(std::abs(rgb[0] - r), std::max(std::abs(rgb[1] - g), std::abs(rgb[2] - b))));
        }
        char name[64]; std::snprintf(name, sizeof name, "round trip profile %d (worst error %d)", p, worst);
        check(worst <= 3, name);
    }

    // ---- profile state defaults
    check(default_profile(0) == BT601_STANDARD && default_profile(1) == BT709_STANDARD, "default profiles");
    check(profile_count == 4 && csc_count == 2 && program_type_count == 2, "profile table sizes");

    // ---- texture format decoding (swizzle = SceGxmTexture::swizzle_format = (format >> 12) & 7)
    {
        auto sw = [](uint32_t fmt) { return (fmt >> 12) & 7u; };
        check(csc_of_420(sw(SCE_GXM_TEXTURE_FORMAT_YUV420P2_CSC0)) == 0 && !chroma_swapped_420(sw(SCE_GXM_TEXTURE_FORMAT_YUV420P2_CSC0)), "YUV420P2_CSC0");
        check(csc_of_420(sw(SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC0)) == 0 && chroma_swapped_420(sw(SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC0)), "YVU420P2_CSC0");
        check(csc_of_420(sw(SCE_GXM_TEXTURE_FORMAT_YUV420P3_CSC1)) == 1 && !chroma_swapped_420(sw(SCE_GXM_TEXTURE_FORMAT_YUV420P3_CSC1)), "YUV420P3_CSC1");
        check(csc_of_420(sw(SCE_GXM_TEXTURE_FORMAT_YVU420P3_CSC1)) == 1 && chroma_swapped_420(sw(SCE_GXM_TEXTURE_FORMAT_YVU420P3_CSC1)), "YVU420P3_CSC1");
        const uint32_t f422[8] = { SCE_GXM_TEXTURE_FORMAT_YUYV422_CSC0, SCE_GXM_TEXTURE_FORMAT_YVYU422_CSC0, SCE_GXM_TEXTURE_FORMAT_UYVY422_CSC0, SCE_GXM_TEXTURE_FORMAT_VYUY422_CSC0,
            SCE_GXM_TEXTURE_FORMAT_YUYV422_CSC1, SCE_GXM_TEXTURE_FORMAT_YVYU422_CSC1, SCE_GXM_TEXTURE_FORMAT_UYVY422_CSC1, SCE_GXM_TEXTURE_FORMAT_VYUY422_CSC1 };
        for (int i = 0; i < 8; i++) {
            check(csc_of_422(sw(f422[i])) == (uint32_t)(i / 4), "422 csc slot");
            Order422 o = order_422(sw(f422[i]));
            check(o.y0 < 4 && o.u < 4 && o.y1 < 4 && o.v < 4 && o.y0 != o.u && o.y0 != o.y1 && o.y0 != o.v && o.u != o.y1 && o.u != o.v && o.y1 != o.v, "422 order is a permutation");
        }
        // YUYV: Y0 U Y1 V, YVYU: Y0 V Y1 U, UYVY: U Y0 V Y1, VYUY: V Y0 U Y1
        Order422 yuyv = order_422(0), yvyu = order_422(1), uyvy = order_422(2), vyuy = order_422(3);
        check(yuyv.y0 == 0 && yuyv.u == 1 && yuyv.y1 == 2 && yuyv.v == 3, "YUYV order");
        check(yvyu.y0 == 0 && yvyu.v == 1 && yvyu.y1 == 2 && yvyu.u == 3, "YVYU order");
        check(uyvy.u == 0 && uyvy.y0 == 1 && uyvy.v == 2 && uyvy.y1 == 3, "UYVY order");
        check(vyuy.v == 0 && vyuy.y0 == 1 && vyuy.u == 2 && vyuy.y1 == 3, "VYUY order");
    }

    // ---- plane layout
    {
        PlaneLayout420 p2 = plane_layout_420(false, 960, 544, 960, 960, 544);
        check(p2.c0_offset == 960 * 544 && p2.c_stride == 960 && p2.chroma_width == 480 && p2.chroma_height == 272 && p2.total_bytes == 960 * 544 * 3 / 2, "P2 960x544 layout");
        PlaneLayout420 p3 = plane_layout_420(true, 960, 544, 960, 960, 544);
        check(p3.c0_offset == 960 * 544 && p3.c1_offset == 960 * 544 * 5 / 4 && p3.c_stride == 480 && p3.total_bytes == 960 * 544 * 3 / 2, "P3 960x544 layout");
        PlaneLayout420 pad = plane_layout_420(false, 100, 60, 104, 128, 64);
        check(pad.c0_offset == 128 * 64 && pad.y_stride == 104 && pad.c_stride == 104 && pad.chroma_width == 50 && pad.chroma_height == 30, "padded layout");
        PlaneLayout420 odd = plane_layout_420(true, 5, 3, 5, 5, 3);
        check(odd.chroma_width == 3 && odd.chroma_height == 2 && odd.c_stride >= 3, "odd sizes round chroma up");
    }

    // ---- CPU conversion = reference of the shader
    {
        const uint32_t W = 8, H = 8;
        // flat colour: every route must give the same colour for every pixel, in P2 / P3, both chroma orders
        for (int p = 0; p < 4; p++) {
            uint8_t expect[3];
            convert_pixel((uint8_t)p, 120, 90, 170, expect);
            std::vector<uint8_t> p2(W * H * 3 / 2), p3(W * H * 3 / 2), swapped(W * H * 3 / 2), dst(W * H * 4);
            for (uint32_t i = 0; i < W * H; i++) p2[i] = p3[i] = swapped[i] = 120;
            for (uint32_t i = 0; i < W * H / 4; i++) {
                p2[W * H + 2 * i] = 90; p2[W * H + 2 * i + 1] = 170;               // UV
                p3[W * H + i] = 90; p3[W * H + W * H / 4 + i] = 170;               // U then V
                swapped[W * H + 2 * i] = 170; swapped[W * H + 2 * i + 1] = 90;     // VU
            }
            for (int variant = 0; variant < 3; variant++) {
                const uint8_t *src = variant == 0 ? p2.data() : variant == 1 ? p3.data() : swapped.data();
                convert_420_to_rgba8((uint8_t)p, variant == 1, variant == 2, dst.data(), W, src, W, H, W, W, H);
                bool ok = true;
                for (uint32_t i = 0; i < W * H; i++)
                    ok &= dst[i * 4] == expect[0] && dst[i * 4 + 1] == expect[1] && dst[i * 4 + 2] == expect[2] && dst[i * 4 + 3] == 255;
                check(ok, "flat 420 colour");
            }
        }
        // chroma ramp: co-sited horizontally (even pixels take the sample, odd pixels the average of two), centred vertically
        {
            std::vector<uint8_t> src(W * H * 3 / 2, 128), dst(W * H * 4);
            for (uint32_t cx = 0; cx < W / 2; cx++) for (uint32_t cy = 0; cy < H / 2; cy++) {
                src[W * H + cy * W + cx * 2] = (uint8_t)(100 + cx * 20);   // U ramp in x
                src[W * H + cy * W + cx * 2 + 1] = 128;
            }
            // use full range 601 so that blue = Y + 1.772 * (U - 128): the blue channel exposes U directly
            convert_420_to_rgba8(BT601_FULL_RANGE, false, false, dst.data(), W, src.data(), W, H, W, W, H);
            auto blue = [&](uint32_t x, uint32_t y) { return (double)dst[(y * W + x) * 4 + 2]; };
            auto u_of = [&](double bl) { return (bl - 128.0) / 1.772 + 128.0; };
            check_near(u_of(blue(2, 3)), 120, 1.0, "even pixel takes the chroma sample");
            check_near(u_of(blue(3, 3)), 130, 1.0, "odd pixel is the average of two samples");
            check_near(u_of(blue(0, 3)), 100, 1.0, "left edge");
            check_near(u_of(blue(7, 3)), 160, 1.0, "right edge clamps to the last sample");
        }
        // chroma ramp in y: midpoint siting, a luma row is 1/4 chroma row away from the nearest sample
        {
            std::vector<uint8_t> src(W * H * 3 / 2, 128), dst(W * H * 4);
            for (uint32_t cy = 0; cy < H / 2; cy++) for (uint32_t cx = 0; cx < W / 2; cx++) {
                src[W * H + cy * W + cx * 2] = (uint8_t)(100 + cy * 40);   // U ramp in y: 100, 140, 180, 220
                src[W * H + cy * W + cx * 2 + 1] = 128;
            }
            convert_420_to_rgba8(BT601_FULL_RANGE, false, false, dst.data(), W, src.data(), W, H, W, W, H);
            auto u_of = [&](uint32_t y) { return ((double)dst[(y * W + 0) * 4 + 2] - 128.0) / 1.772 + 128.0; };
            check_near(u_of(2), 130, 1.0, "row 2 = sample 0 + 3/4 step");
            check_near(u_of(3), 150, 1.0, "row 3 = sample 1 + 1/4 step");
            check_near(u_of(0), 100, 1.0, "top edge clamps");
        }
        // 422: all four byte orders describe the same picture
        {
            const uint32_t w = 6, h = 2;
            uint8_t expect[3];
            convert_pixel(BT709_STANDARD, 150, 70, 190, expect);
            for (uint32_t order = 0; order < 4; order++) {
                Order422 o = order_422(order);
                std::vector<uint8_t> src(w * h * 2), dst(w * h * 4);
                for (uint32_t m = 0; m < w * h / 2; m++) { src[m * 4 + o.y0] = 150; src[m * 4 + o.y1] = 150; src[m * 4 + o.u] = 70; src[m * 4 + o.v] = 190; }
                convert_422_to_rgba8(BT709_STANDARD, order, dst.data(), w, src.data(), w, h, w);
                bool ok = true;
                for (uint32_t i = 0; i < w * h; i++) ok &= dst[i * 4] == expect[0] && dst[i * 4 + 1] == expect[1] && dst[i * 4 + 2] == expect[2] && dst[i * 4 + 3] == 255;
                check(ok, "422 byte order");
            }
            // luma of the two pixels of a macro pixel comes from y0 / y1
            std::vector<uint8_t> src = { 50, 128, 200, 128 }, dst(2 * 4);
            convert_422_to_rgba8(BT601_FULL_RANGE, 0, dst.data(), 2, src.data(), 2, 1, 2);
            check(dst[0] == 50 && dst[4] == 200, "422 YUYV luma of both pixels");
        }
        // odd sizes must not read outside of the planes
        {
            const uint32_t w = 5, h = 3;
            std::vector<uint8_t> src(w * h + 3 * 2 * 2 + 16, 100), dst(w * h * 4);
            convert_420_to_rgba8(BT601_STANDARD, false, false, dst.data(), w, src.data(), w, h, w, w, h);
            check(dst[3] == 255, "odd size 420");
        }
    }

    // ---- route selection from the probed capabilities
    {
        RouteCaps none;
        RouteCaps shader; shader.shader = true;
        RouteCaps both; both.shader = true; both.ycbcr[0] = both.ycbcr[1] = true;
        check(select_route(none, Kind::P2) == Route::Cpu, "no capability -> cpu");
        check(select_route(shader, Kind::P2) == Route::Shader, "shader route");
        check(select_route(both, Kind::P2) == Route::Ycbcr && select_route(both, Kind::P3) == Route::Ycbcr, "ycbcr preferred when supported");
        check(select_route(both, Kind::Packed422) == Route::Shader, "ycbcr missing for 4:2:2 -> shader");
        check(select_route(both, Kind::P2, (int)Route::Cpu) == Route::Cpu, "forced cpu");
        check(select_route(both, Kind::P2, (int)Route::Shader) == Route::Shader, "forced shader");
        check(select_route(shader, Kind::P2, (int)Route::Ycbcr) == Route::Shader, "forced ycbcr unsupported -> shader");
        check(select_route(none, Kind::P2, (int)Route::Ycbcr) == Route::Cpu, "forced ycbcr, nothing supported -> cpu");
        RouteCaps ycbcr_only; ycbcr_only.ycbcr[2] = true;
        check(select_route(ycbcr_only, Kind::Packed422) == Route::Ycbcr, "ycbcr without the shader route");
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
