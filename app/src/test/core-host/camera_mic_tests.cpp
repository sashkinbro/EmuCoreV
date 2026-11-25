// Host tests for the SDL-free camera policy (attribute store, crop geometry, tone adjustments, dark
// fallback colour) and the microphone (sceAudioIn) settings helpers.

#include <audio/in_settings.h>
#include <camera/camera_policy.h>

#include <gtest/gtest.h>

#include <vector>

using ecv_camera::Attr;
using ecv_camera::AttrStatus;
using ecv_camera::AttributeStore;

TEST(CameraPolicy, DarkFallbackColourIsOpaqueBlack) {
    EXPECT_EQ(ecv_camera::kDarkFrameColor, 0xFF000000u);
}

TEST(CameraPolicy, SetThenGetReturnsTheStoredValue) {
    AttributeStore s;
    EXPECT_EQ(s.get(Attr::Zoom), 10);
    EXPECT_EQ(s.set(Attr::Zoom, 20), AttrStatus::Ok);
    EXPECT_EQ(s.get(Attr::Zoom), 20);
    EXPECT_EQ(s.set(Attr::Brightness, 200), AttrStatus::Ok);
    EXPECT_EQ(s.get(Attr::Brightness), 200);
    EXPECT_EQ(s.set(Attr::ISO, 400), AttrStatus::Ok);
    EXPECT_EQ(s.get(Attr::ISO), 400);
    EXPECT_EQ(s.set(Attr::Reverse, 3), AttrStatus::Ok);
    EXPECT_EQ(s.get(Attr::Reverse), 3);
}

TEST(CameraPolicy, OutOfRangeValuesAreRejectedWithoutChangingTheStoredValue) {
    AttributeStore s;
    EXPECT_EQ(s.set(Attr::Zoom, 9), AttrStatus::BadValue);
    EXPECT_EQ(s.set(Attr::Zoom, 41), AttrStatus::BadValue);
    EXPECT_EQ(s.get(Attr::Zoom), 10);
    EXPECT_EQ(s.set(Attr::EV, -21), AttrStatus::BadValue);
    EXPECT_EQ(s.set(Attr::Effect, 7), AttrStatus::BadValue);
    EXPECT_EQ(s.set(Attr::Reverse, 4), AttrStatus::BadValue);
    EXPECT_EQ(s.set(Attr::Reverse, -1), AttrStatus::BadValue);
    EXPECT_EQ(s.set(Attr::ISO, 150), AttrStatus::BadValue);
    EXPECT_EQ(s.set(Attr::WhiteBalance, 3), AttrStatus::BadValue);
    EXPECT_EQ(s.set(Attr::Backlight, 2), AttrStatus::BadValue);
    EXPECT_EQ(s.get(Attr::Reverse), 0);
}

TEST(CameraPolicy, GainCannotBeSetOrReadLikeOnTheOriginalVita) {
    AttributeStore s;
    EXPECT_EQ(s.set(Attr::Gain, 1), AttrStatus::Unknown);
    EXPECT_EQ(s.get(Attr::Gain), 0);
}

TEST(CameraPolicy, ResetRestoresTheSensorDefaults) {
    AttributeStore s;
    s.set(Attr::Zoom, 30);
    s.set(Attr::Saturation, 0);
    s.set(Attr::Reverse, 1);
    s.reset();
    EXPECT_EQ(s.get(Attr::Zoom), 10);
    EXPECT_EQ(s.get(Attr::Saturation), 10);
    EXPECT_EQ(s.get(Attr::Brightness), 128);
    EXPECT_EQ(s.get(Attr::Reverse), 0);
    EXPECT_EQ(s.get(Attr::AntiFlicker), 1);
    EXPECT_TRUE(ecv_camera::adjust_from(s).identity());
}

TEST(CameraPolicy, OnlyZoomAndToneAttributesTouchThePixels) {
    EXPECT_TRUE(AttributeStore::affects_pixels(Attr::Zoom));
    EXPECT_TRUE(AttributeStore::affects_pixels(Attr::Brightness));
    EXPECT_TRUE(AttributeStore::affects_pixels(Attr::Effect));
    EXPECT_FALSE(AttributeStore::affects_pixels(Attr::ISO));
    EXPECT_FALSE(AttributeStore::affects_pixels(Attr::Reverse)); // applied while copying to the guest
}

TEST(CameraPolicy, WideSourceIsCentreCroppedToTheTargetAspectWithoutStretching) {
    // 1000x500 (2:1) into 640x480 (4:3): full height, width 500*4/3 = 666, centred.
    const auto r = ecv_camera::center_crop(1000, 500, 640, 480);
    EXPECT_EQ(r.h, 500);
    EXPECT_EQ(r.w, 666);
    EXPECT_EQ(r.x, 167);
    EXPECT_EQ(r.y, 0);
}

TEST(CameraPolicy, TallSourceIsCropVertically) {
    // 3:4 portrait photo into a 640x480 target keeps full width and the centre band.
    const auto r = ecv_camera::center_crop(480, 640, 640, 480);
    EXPECT_EQ(r.w, 480);
    EXPECT_EQ(r.h, 360);
    EXPECT_EQ(r.x, 0);
    EXPECT_EQ(r.y, 140);
}

TEST(CameraPolicy, SameAspectIsNotCropped) {
    const auto r = ecv_camera::center_crop(1280, 960, 640, 480);
    EXPECT_EQ((ecv_camera::Rect{ 0, 0, 1280, 960 }), r);
}

TEST(CameraPolicy, SixteenByNineModesCropAFourByThreeSensor) {
    const auto r = ecv_camera::center_crop(640, 480, 640, 360);
    EXPECT_EQ(r.w, 640);
    EXPECT_EQ(r.h, 360);
    EXPECT_EQ(r.y, 60);
}

TEST(CameraPolicy, DigitalZoomShrinksTheCropAroundTheCentre) {
    const auto r = ecv_camera::center_crop(640, 480, 640, 480, 20);
    EXPECT_EQ(r.w, 320);
    EXPECT_EQ(r.h, 240);
    EXPECT_EQ(r.x, 160);
    EXPECT_EQ(r.y, 120);
    EXPECT_EQ(ecv_camera::center_crop(640, 480, 640, 480, 400).w, 160); // clamped to 4.0x
}

TEST(CameraPolicy, DegenerateInputsGiveAnEmptyRect) {
    EXPECT_EQ(ecv_camera::center_crop(0, 10, 4, 4).w, 0);
    EXPECT_EQ(ecv_camera::center_crop(10, 10, 0, 4).w, 0);
}

namespace {
std::vector<unsigned char> solid_argb(int w, int h, unsigned char r, unsigned char g, unsigned char b) {
    std::vector<unsigned char> px(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i] = b;
        px[i + 1] = g;
        px[i + 2] = r;
        px[i + 3] = 255;
    }
    return px;
}
} // namespace

TEST(CameraPolicy, IdentityAdjustmentLeavesPixelsAlone) {
    auto px = solid_argb(2, 2, 10, 20, 30);
    const auto copy = px;
    ecv_camera::adjust_argb(px.data(), 2, 2, 8, ecv_camera::Adjust{});
    EXPECT_EQ(px, copy);
}

TEST(CameraPolicy, BrightnessAndNegativeEffectChangeThePixels) {
    auto px = solid_argb(1, 1, 100, 100, 100);
    ecv_camera::Adjust bright;
    bright.brightness = 148; // +20
    ecv_camera::adjust_argb(px.data(), 1, 1, 4, bright);
    EXPECT_EQ(px[2], 120);

    auto neg = solid_argb(1, 1, 10, 200, 0);
    ecv_camera::Adjust effect;
    effect.effect = 1;
    ecv_camera::adjust_argb(neg.data(), 1, 1, 4, effect);
    EXPECT_EQ(neg[2], 245);
    EXPECT_EQ(neg[1], 55);
    EXPECT_EQ(neg[0], 255);
    EXPECT_EQ(neg[3], 255); // alpha untouched
}

TEST(CameraPolicy, ZeroSaturationGivesGrey) {
    auto px = solid_argb(1, 1, 200, 50, 10);
    ecv_camera::Adjust a;
    a.saturation = 0;
    ecv_camera::adjust_argb(px.data(), 1, 1, 4, a);
    EXPECT_EQ(px[0], px[1]);
    EXPECT_EQ(px[1], px[2]);
}

TEST(CameraPolicy, AdjustmentSaturatesInsteadOfWrapping) {
    auto px = solid_argb(1, 1, 250, 250, 250);
    ecv_camera::Adjust a;
    a.ev = 20; // +2 stops
    ecv_camera::adjust_argb(px.data(), 1, 1, 4, a);
    EXPECT_EQ(px[0], 255);
    EXPECT_EQ(px[1], 255);
    EXPECT_EQ(px[2], 255);
}

TEST(MicSettings, GainIsClampedToTheUiRange) {
    EXPECT_EQ(ecv_audio_in::clamp_gain(-5), 0);
    EXPECT_EQ(ecv_audio_in::clamp_gain(100), 100);
    EXPECT_EQ(ecv_audio_in::clamp_gain(9999), ecv_audio_in::kGainMaxPercent);
}

TEST(MicSettings, ApplyGainScalesAndSaturates) {
    int16_t s[4] = { 1000, -1000, 30000, -30000 };
    ecv_audio_in::apply_gain(s, 4, 200);
    EXPECT_EQ(s[0], 2000);
    EXPECT_EQ(s[1], -2000);
    EXPECT_EQ(s[2], 32767);
    EXPECT_EQ(s[3], -32768);
    int16_t unity[2] = { 123, -456 };
    ecv_audio_in::apply_gain(unity, 2, 100);
    EXPECT_EQ(unity[0], 123);
    int16_t mute[2] = { 123, -456 };
    ecv_audio_in::apply_gain(mute, 2, 0);
    EXPECT_EQ(mute[0], 0);
    EXPECT_EQ(mute[1], 0);
}

TEST(MicSettings, MuteStatusReflectsSettingAndPermission) {
    EXPECT_EQ(ecv_audio_in::mute_status(true, true), 0);
    EXPECT_EQ(ecv_audio_in::mute_status(false, true), 1);
    EXPECT_EQ(ecv_audio_in::mute_status(true, false), 1);
    EXPECT_EQ(ecv_audio_in::mute_status(false, false), 1);
}

TEST(MicSettings, CaptureIsOnlyWantedWhenEnabledAndNotSuspended) {
    EXPECT_TRUE(ecv_audio_in::capture_wanted(true, false));
    EXPECT_FALSE(ecv_audio_in::capture_wanted(false, false));
    EXPECT_FALSE(ecv_audio_in::capture_wanted(true, true));
}

TEST(MicSettings, DefaultsAreEnabledUnityGainSystemDevice) {
    ecv_audio_in::Settings s;
    EXPECT_TRUE(s.enabled.load());
    EXPECT_EQ(s.gain_percent.load(), 100);
    EXPECT_EQ(s.device_id.load(), 0);
    EXPECT_FALSE(s.suspended.load());
}

TEST(MicSettings, StalledStreamIsDetectedAfterEnoughEmptyGrains) {
    EXPECT_FALSE(ecv_audio_in::stream_stalled(ecv_audio_in::kStallGrains - 1));
    EXPECT_TRUE(ecv_audio_in::stream_stalled(ecv_audio_in::kStallGrains));
}

TEST(MicSettings, GuestSelectionsAreValidated) {
    EXPECT_TRUE(ecv_audio_in::valid_guest_gain(0));
    EXPECT_TRUE(ecv_audio_in::valid_guest_gain(15));
    EXPECT_FALSE(ecv_audio_in::valid_guest_gain(-1));
    EXPECT_TRUE(ecv_audio_in::valid_guest_input(0));
    EXPECT_TRUE(ecv_audio_in::valid_guest_input(1));
    EXPECT_FALSE(ecv_audio_in::valid_guest_input(2));
}
