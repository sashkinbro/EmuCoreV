// EmuCoreV camera policy: attribute store, centre-crop geometry and tone adjustments.
// Header-only and free of SDL/emulator types so the logic is covered by the host tests
// (app/src/test/core-host/camera_mic_tests.cpp).
//
// Values that are NOT recovered from firmware are marked "provisional": the attribute ranges
// follow the SceCamera SDK enums in camera.h, the pixel effects are an approximation of what the
// Vita sensor ISP does and are not the original curves.
#ifndef ECV_CAMERA_POLICY_H
#define ECV_CAMERA_POLICY_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ecv_camera {

// Opaque black: what a game sees when no usable camera / image exists (a dark, working camera).
constexpr uint32_t kDarkFrameColor = 0xFF000000u;

// Numbers from the disassembled sceCameraGet/Set* exports. Mirrored by CameraAttributes in camera.h.
enum class Attr : int {
    Saturation = 0xa,
    Brightness = 0xb,
    Contrast = 0xc,
    Sharpness = 0xd,
    Reverse = 0xe,
    Effect = 0xf,
    EV = 0x10,
    Zoom = 0x11,
    AntiFlicker = 0x12,
    Gain, // unused: cannot be get/set on an original Vita
    ISO = 0x14,
    WhiteBalance = 0x16,
    Backlight = 0x17,
    Nightmode = 0x18,
    NoiseReduction = 0x19,
    AutoControlHold = 0x1a,
    ExposureCeiling = 0x1b,
    ImageQuality = 0x1c,
    SharpnessOff
};

enum class AttrStatus { Ok, BadValue, Unknown };

constexpr int kZoomMin = 10; // 1.0x
constexpr int kZoomMax = 40; // 4.0x

class AttributeStore {
public:
    AttributeStore() { reset(); }

    // A real Vita starts every open() with the sensor defaults.
    void reset() {
        std::fill(std::begin(values_), std::end(values_), 0);
        values_[idx(Attr::Saturation)] = 10; // SCE_CAMERA_SATURATION_10
        values_[idx(Attr::Brightness)] = 128;
        values_[idx(Attr::Contrast)] = 128;
        values_[idx(Attr::Sharpness)] = 1; // SCE_CAMERA_SHARPNESS_100
        values_[idx(Attr::Zoom)] = kZoomMin;
        values_[idx(Attr::AntiFlicker)] = 1; // AUTO
        values_[idx(Attr::ISO)] = 1; // AUTO
    }

    int get(Attr a) const {
        const int i = idx(a);
        return i >= 0 && i < kCount ? values_[i] : 0;
    }

    // Provisional ranges: taken from the SDK enums (camera.h). Brightness/Contrast use 0-255.
    AttrStatus set(Attr a, int value) {
        if (a == Attr::Gain)
            return AttrStatus::Unknown;
        if (!valid(a, value))
            return AttrStatus::BadValue;
        values_[idx(a)] = value;
        return AttrStatus::Ok;
    }

    static bool valid(Attr a, int v) {
        switch (a) {
        case Attr::Saturation: return v >= 0 && v <= 40;
        case Attr::Brightness:
        case Attr::Contrast: return v >= 0 && v <= 255;
        case Attr::Sharpness: return v >= 1 && v <= 4;
        case Attr::Reverse: return v >= 0 && v <= 3;
        case Attr::Effect: return v >= 0 && v <= 6;
        case Attr::EV: return v >= -20 && v <= 20;
        case Attr::Zoom: return v >= kZoomMin && v <= kZoomMax;
        case Attr::AntiFlicker: return v >= 1 && v <= 3;
        case Attr::ISO: return v == 1 || v == 100 || v == 200 || v == 400;
        case Attr::WhiteBalance: return v == 0 || v == 1 || v == 2 || v == 4;
        case Attr::Backlight: return v == 0 || v == 1;
        case Attr::Nightmode: return v >= 0 && v <= 3;
        case Attr::NoiseReduction:
        case Attr::AutoControlHold:
        case Attr::ExposureCeiling:
        case Attr::ImageQuality:
        case Attr::SharpnessOff: return v >= 0 && v <= 255; // undocumented, provisional
        case Attr::Gain: return false;
        }
        return false;
    }

    // Only these change the pixels we deliver; the others are stored and reported back.
    static bool affects_pixels(Attr a) {
        switch (a) {
        case Attr::Saturation:
        case Attr::Brightness:
        case Attr::Contrast:
        case Attr::Effect:
        case Attr::EV:
        case Attr::Zoom: return true;
        default: return false;
        }
    }

private:
    static constexpr int kCount = static_cast<int>(Attr::SharpnessOff) + 1;
    static int idx(Attr a) { return static_cast<int>(a); }
    int values_[kCount];
};

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool operator==(const Rect &o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
};

// Largest centred rectangle of the source with the destination aspect ratio, shrunk by the digital
// zoom (zoom_x10 = 10 is 1.0x). Never distorts: scaling the result to dw x dh keeps the proportions.
inline Rect center_crop(int sw, int sh, int dw, int dh, int zoom_x10 = kZoomMin) {
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
        return {};
    const int64_t zoom = std::clamp(zoom_x10, kZoomMin, kZoomMax);
    int64_t w, h;
    if (static_cast<int64_t>(sw) * dh > static_cast<int64_t>(sh) * dw) { // source is wider
        h = sh;
        w = static_cast<int64_t>(sh) * dw / dh;
    } else {
        w = sw;
        h = static_cast<int64_t>(sw) * dh / dw;
    }
    w = std::clamp<int64_t>(w * kZoomMin / zoom, 1, sw);
    h = std::clamp<int64_t>(h * kZoomMin / zoom, 1, sh);
    return { static_cast<int>((sw - w) / 2), static_cast<int>((sh - h) / 2), static_cast<int>(w), static_cast<int>(h) };
}

struct Adjust {
    int brightness = 128; // 0-255, 128 neutral
    int contrast = 128; // 0-255, 128 neutral
    int ev = 0; // tenths of a stop
    int saturation = 10; // 10 = 1.0
    int effect = 0; // SceCameraEffect

    bool identity() const { return brightness == 128 && contrast == 128 && ev == 0 && saturation == 10 && effect == 0; }
};

inline Adjust adjust_from(const AttributeStore &s) {
    Adjust a;
    a.brightness = s.get(Attr::Brightness);
    a.contrast = s.get(Attr::Contrast);
    a.ev = s.get(Attr::EV);
    a.saturation = s.get(Attr::Saturation);
    a.effect = s.get(Attr::Effect);
    return a;
}

inline uint8_t clamp8(int v) { return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); }

// Applies the adjustments in place to ARGB8888 pixels (byte order B,G,R,A in memory, little endian).
// Provisional approximation: exposure gain 2^(ev/10), linear contrast around mid grey, additive
// brightness, saturation around BT.601-ish luma, then the colour effect.
inline void adjust_argb(unsigned char *pixels, int width, int height, int pitch, const Adjust &adj) {
    if (!pixels || width <= 0 || height <= 0 || adj.identity())
        return;
    uint8_t lut[256];
    const double gain = std::exp2(adj.ev / 10.0);
    for (int v = 0; v < 256; ++v) {
        double x = v * gain;
        x = (x - 128.0) * adj.contrast / 128.0 + 128.0;
        x += adj.brightness - 128;
        lut[v] = clamp8(static_cast<int>(std::lround(x)));
    }
    for (int row = 0; row < height; ++row) {
        unsigned char *p = pixels + static_cast<size_t>(row) * pitch;
        for (int col = 0; col < width; ++col, p += 4) {
            int b = lut[p[0]], g = lut[p[1]], r = lut[p[2]];
            int gray = (77 * r + 150 * g + 29 * b) >> 8;
            if (adj.saturation != 10) {
                r = gray + (r - gray) * adj.saturation / 10;
                g = gray + (g - gray) * adj.saturation / 10;
                b = gray + (b - gray) * adj.saturation / 10;
                gray = (77 * clamp8(r) + 150 * clamp8(g) + 29 * clamp8(b)) >> 8;
            }
            switch (adj.effect) {
            case 1: r = 255 - r; g = 255 - g; b = 255 - b; break; // NEGATIVE
            case 2: r = g = b = gray; break; // BLACKWHITE
            case 3: { // SEPIA
                const int cr = clamp8(r), cg = clamp8(g), cb = clamp8(b);
                r = (cr * 101 + cg * 197 + cb * 48) >> 8;
                g = (cr * 89 + cg * 175 + cb * 43) >> 8;
                b = (cr * 70 + cg * 137 + cb * 34) >> 8;
                break;
            }
            case 4: r = gray / 3; g = gray / 2; b = gray; break; // BLUE
            case 5: r = gray; g = gray / 3; b = gray / 3; break; // RED
            case 6: r = gray / 3; g = gray; b = gray / 3; break; // GREEN
            default: break;
            }
            p[0] = clamp8(b);
            p[1] = clamp8(g);
            p[2] = clamp8(r);
        }
    }
}

} // namespace ecv_camera

#endif
