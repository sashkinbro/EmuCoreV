// EmuCoreV peripherals regression cases. These fixtures never claim a real camera capture.
#include "../../main/cpp/vita3k/vita3k/camera/include/camera/camera.h"
#include "../../main/cpp/vita3k/vita3k/camera/include/camera/frame_copy.h"
#include "../../main/cpp/vita3k/vita3k/camera/include/camera/frame_convert.h"
#include "../../main/cpp/vita3k/vita3k/camera/include/camera/read_abi.h"
#include "../../main/cpp/vita3k/external/sdl/src/camera/android/SDL_camera_android_yuv.h"
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_error.h>

namespace {
void peripheral_touch_fixture(EmuEnvState &env) {
    env.display.viewport_x = env.display.viewport_y = 0;
    env.display.viewport_w = env.display.viewport_drawable_w = 960;
    env.display.viewport_h = env.display.viewport_drawable_h = 544;
    set_touch_sampling_state(env.touch, SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    set_touch_sampling_state(env.touch, SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
}
SDL_TouchFingerEvent peripheral_finger(SDL_FingerID id, float x, float y, SDL_TouchID device = 10) {
    SDL_TouchFingerEvent event{};
    event.type = SDL_EVENT_FINGER_DOWN;
    event.touchID = device;
    event.fingerID = id;
    event.x = x;
    event.y = y;
    return event;
}
}

TEST(GuestRearTouch, mapped_rear_and_screen_front_are_independent) {
    EmuEnvState env;
    peripheral_touch_fixture(env);
    auto finger = peripheral_finger(1, .5f, .5f);
    handle_touch_event(env.touch, finger);
    set_rear_touch_contact(env.touch, 0, true, 0.f, 0.f);
    set_rear_touch_contact(env.touch, 1, true, 1.f, 1.f);
    touch_vsync_update(env);
    const auto &front = env.touch.touch_buffers[env.touch.touch_buffer_idx][0];
    const auto &rear = env.touch.touch_buffers[env.touch.touch_buffer_idx][1];
    ASSERT_EQ(front.reportNum, 1u);
    ASSERT_EQ(rear.reportNum, 2u);
    EXPECT_EQ(front.report[0].x, 960);
    EXPECT_EQ(front.report[0].y, 544);
    EXPECT_EQ(rear.report[0].x, 0);
    EXPECT_EQ(rear.report[0].y, 108);
    EXPECT_EQ(rear.report[1].x, 1919);
    EXPECT_EQ(rear.report[1].y, 889);
    EXPECT_NE(front.report[0].id, rear.report[0].id);
    const auto id = rear.report[0].id;
    set_rear_touch_contact(env.touch, 0, true, .5f, .5f);
    touch_vsync_update(env);
    EXPECT_EQ(env.touch.touch_buffers[env.touch.touch_buffer_idx][1].report[0].id, id);
    set_rear_touch_contact(env.touch, 0, false, 0, 0);
    touch_vsync_update(env);
    EXPECT_EQ(env.touch.touch_buffers[env.touch.touch_buffer_idx][0].reportNum, 1u);
    EXPECT_EQ(env.touch.touch_buffers[env.touch.touch_buffer_idx][1].reportNum, 1u);
}

TEST(GuestRearTouch, physical_touchpad_does_not_displace_screen_contacts) {
    EmuEnvState env;
    peripheral_touch_fixture(env);
    auto finger = peripheral_finger(1, .25f, .5f);
    handle_touch_event(env.touch, finger);
    SDL_GamepadTouchpadEvent pad{};
    pad.type = SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN;
    pad.which = 100;
    pad.x = .75f; pad.y = .5f;
    handle_touchpad_event(env.touch, pad);
    touch_vsync_update(env);
    const auto &sample = env.touch.touch_buffers[env.touch.touch_buffer_idx][0];
    ASSERT_EQ(sample.reportNum, 2u);
    EXPECT_EQ(sample.report[0].x, 480);
    EXPECT_EQ(sample.report[1].x, 1439);
    EXPECT_NE(sample.report[0].id, sample.report[1].id);
}

TEST(GuestRearTouch, mirrored_screen_honors_six_front_four_rear_capacity) {
    EmuEnvState env;
    peripheral_touch_fixture(env);
    set_touch_mode(env.touch, 2);
    for (int i = 0; i < 8; ++i) {
        auto finger = peripheral_finger(i, .1f * i, .5f);
        handle_touch_event(env.touch, finger);
    }
    touch_vsync_update(env);
    EXPECT_EQ(env.touch.touch_buffers[env.touch.touch_buffer_idx][0].reportNum, 6u);
    EXPECT_EQ(env.touch.touch_buffers[env.touch.touch_buffer_idx][1].reportNum, 4u);
}

TEST(GuestRearTouch, sampling_stop_and_cancel_clear_history_without_resetting_port_mode) {
    EmuEnvState env;
    peripheral_touch_fixture(env);
    set_touch_mode(env.touch, 1);
    set_rear_touch_contact(env.touch, 2, true, .5f, .5f);
    touch_vsync_update(env);
    ASSERT_EQ(env.touch.touch_buffers[env.touch.touch_buffer_idx][1].reportNum, 1u);
    set_touch_sampling_state(env.touch, 1, SCE_TOUCH_SAMPLING_STATE_STOP);
    for (const auto &ports : env.touch.touch_buffers) EXPECT_EQ(ports[1].reportNum, 0u);
    set_touch_sampling_state(env.touch, 1, SCE_TOUCH_SAMPLING_STATE_START);
    clear_touch_contacts(env.touch);
    touch_vsync_update(env);
    EXPECT_EQ(env.touch.touch_buffers[env.touch.touch_buffer_idx][1].reportNum, 0u);
    EXPECT_EQ(env.touch.touchscreen_port, SCE_TOUCH_PORT_BACK);
    EXPECT_EQ(env.touch.touch_mode[1], SCE_TOUCH_SAMPLING_STATE_START);
}

TEST(GuestRearTouch, out_of_panel_points_do_not_create_or_move_contacts) {
    EmuEnvState env;
    peripheral_touch_fixture(env);
    set_rear_touch_contact(env.touch, 0, true, -.1f, .5f);
    set_rear_touch_contact(env.touch, 4, true, .5f, .5f);
    touch_vsync_update(env);
    EXPECT_EQ(env.touch.touch_buffers[env.touch.touch_buffer_idx][1].reportNum, 0u);
    set_rear_touch_contact(env.touch, 0, true, .25f, .5f);
    set_rear_touch_contact(env.touch, 0, true, 2.f, .5f);
    touch_vsync_update(env);
    ASSERT_EQ(env.touch.touch_buffers[env.touch.touch_buffer_idx][1].reportNum, 1u);
    EXPECT_EQ(env.touch.touch_buffers[env.touch.touch_buffer_idx][1].report[0].x, 480);
}

TEST(GuestRearTouch, same_finger_number_on_two_controllers_releases_only_its_source) {
    TouchState state;
    SDL_GamepadTouchpadEvent a{};
    a.type = SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN;
    a.which = 100;
    auto b = a; b.which = 200; b.x = .75f;
    handle_touchpad_event(state, a);
    handle_touchpad_event(state, b);
    ASSERT_EQ(state.touchpad_finger_count, 2);
    a.type = SDL_EVENT_GAMEPAD_TOUCHPAD_UP;
    handle_touchpad_event(state, a);
    ASSERT_EQ(state.touchpad_finger_count, 1);
    EXPECT_EQ(state.touchpad_buffer[0].x, .75f);
}

TEST(GuestRearTouch, unplugging_one_controller_preserves_other_live_contacts_and_history) {
    EmuEnvState env;
    peripheral_touch_fixture(env);
    auto finger = peripheral_finger(1,.25f,.5f);
    handle_touch_event(env.touch,finger);
    set_rear_touch_contact(env.touch,0,true,.5f,.5f);
    SDL_GamepadTouchpadEvent a{};
    a.type=SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN; a.which=100; a.x=.5f; a.y=.5f;
    auto b=a; b.which=200; b.x=.75f;
    handle_touchpad_event(env.touch,a); handle_touchpad_event(env.touch,b);
    touch_vsync_update(env);
    release_touchpad_device(env.touch,100);
    ASSERT_EQ(env.touch.touchpad_finger_count,1);
    EXPECT_EQ(env.touch.touchpad_devices[0],200u);
    EXPECT_EQ(env.touch.finger_count,1);
    EXPECT_TRUE(env.touch.rear_contact_down[0]);
    EXPECT_EQ(env.touch.touch_buffers[env.touch.touch_buffer_idx][0].reportNum,2u);
    EXPECT_EQ(env.touch.touch_buffers[env.touch.touch_buffer_idx][1].reportNum,1u);
}

TEST(GuestCameraBuffers, flexible_android_chroma_is_repacked_in_uv_order) {
    const unsigned char y[] = {1,2,3,4,99,99,5,6,7,8};
    const unsigned char u[] = {10,99,11};
    const unsigned char v[] = {20,99,21};
    std::array<unsigned char, 16> out{}; out.fill(0xCD);
    ASSERT_TRUE(ECV_PackAndroidYuv420(out.data(), 12, 4, 2, y, 10, 6, 1, u, 3, 4, 2, v, 3, 4, 2));
    const std::array<unsigned char, 16> expected = {1,2,3,4,5,6,7,8,10,20,11,21,0xCD,0xCD,0xCD,0xCD};
    EXPECT_EQ(out, expected);
}

TEST(GuestCameraBuffers, yv12_guest_copy_honors_uv_order_and_padding) {
    const unsigned char frame[] = {1,2,3,4,99,99,5,6,7,8,99,99,20,21,99,10,11,99};
    std::array<unsigned char, 16> y{}; y.fill(0xCD);
    std::array<unsigned char, 4> u{}, v{}; u.fill(0xCD); v.fill(0xCD);
    ASSERT_TRUE(ecv_camera_copy(3,4,2,2,0,frame,6,sizeof(frame),y.data(),y.size(),u.data(),u.size(),v.data(),v.size()));
    EXPECT_EQ(y[4],0xCD); EXPECT_EQ(y[6],5);
    EXPECT_EQ(u[0],10); EXPECT_EQ(u[1],11);
    EXPECT_EQ(v[0],20); EXPECT_EQ(v[1],21);
}

TEST(GuestCameraBuffers, short_guest_buffers_fail_without_partial_writes) {
    std::array<unsigned char, 32> frame{};
    std::array<unsigned char, 8> out{}; out.fill(0xCD);
    EXPECT_FALSE(ecv_camera_copy(4,4,2,0,0,frame.data(),16,frame.size(),out.data(),out.size(),nullptr,0,nullptr,0));
    for (const auto value : out) EXPECT_EQ(value,0xCD);
}

TEST(GuestCameraBuffers, sdl_rgb_conversion_matches_guest_argb_and_abgr_byte_order) {
    const unsigned char red[] = {255,0,0,255,0,0,255,0,0,255,0,0};
    auto *rgb = SDL_CreateSurfaceFrom(2,2,SDL_PIXELFORMAT_RGB24,const_cast<unsigned char *>(red),6);
    ASSERT_NE(rgb,nullptr);
    for (const auto format : {SDL_PIXELFORMAT_ARGB8888,SDL_PIXELFORMAT_ABGR8888}) {
        auto *converted = SDL_ConvertSurface(rgb,format);
        ASSERT_NE(converted,nullptr);
        const auto *bytes = static_cast<const unsigned char *>(converted->pixels);
        EXPECT_EQ(bytes[0],format == SDL_PIXELFORMAT_ARGB8888 ? 0 : 255);
        EXPECT_EQ(bytes[1],0);
        EXPECT_EQ(bytes[2],format == SDL_PIXELFORMAT_ARGB8888 ? 255 : 0);
        EXPECT_EQ(bytes[3],255);
        SDL_DestroySurface(converted);
    }
    SDL_DestroySurface(rgb);
}

TEST(GuestCameraBuffers, legacy_and_extended_read_buffer_offsets_are_distinct) {
    std::array<unsigned int,18> read{};
    read[8]=100; read[11]=0x81000000;
    unsigned int buffers[6]{};
    ASSERT_TRUE(ecv_camera_read_buffers(reinterpret_cast<const unsigned char *>(read.data()),56,buffers));
    EXPECT_EQ(buffers[0],100u); EXPECT_EQ(buffers[3],0x81000000u);
    read[12]=200; read[15]=0x82000000;
    ASSERT_TRUE(ecv_camera_read_buffers(reinterpret_cast<const unsigned char *>(read.data()),72,buffers));
    EXPECT_EQ(buffers[0],200u); EXPECT_EQ(buffers[3],0x82000000u);
    EXPECT_FALSE(ecv_camera_read_buffers(reinterpret_cast<const unsigned char *>(read.data()),32,buffers));
}

TEST(GuestCameraBuffers, explicit_guest_range_conversion_changes_luma_not_just_metadata) {
    unsigned char red[] = {255,0,0,255,0,0,255,0,0,255,0,0};
    auto *rgb=SDL_CreateSurfaceFrom(2,2,SDL_PIXELFORMAT_RGB24,red,6);
    ASSERT_NE(rgb,nullptr);
    auto *full=SDL_ConvertSurfaceAndColorspace(rgb,SDL_PIXELFORMAT_YV12,nullptr,SDL_COLORSPACE_BT601_FULL,0);
    auto *limited=SDL_ConvertSurfaceAndColorspace(rgb,SDL_PIXELFORMAT_YV12,nullptr,SDL_COLORSPACE_BT601_LIMITED,0);
    ASSERT_NE(full,nullptr);ASSERT_NE(limited,nullptr);
    const int full_y=*static_cast<unsigned char *>(full->pixels);
    const int limited_y=*static_cast<unsigned char *>(limited->pixels);
    EXPECT_NE(full_y,limited_y);
    EXPECT_NEAR(full_y,76,1);
    EXPECT_NEAR(limited_y,82,1);
    SDL_DestroySurface(full);SDL_DestroySurface(limited);SDL_DestroySurface(rgb);
}

TEST(GuestCameraLifecycle, raw8_is_rejected_without_opening_and_unsupported_controls_are_errors) {
    Camera camera;
    SceCameraInfo info{}; info.size=sizeof(info); info.format=SCE_CAMERA_FORMAT_RAW8;
    EXPECT_EQ(static_cast<uint32_t>(camera.open(&info,0,0)),SCE_CAMERA_ERROR_FORMAT_UNKNOWN);
    EXPECT_FALSE(camera.is_opened);
    EXPECT_EQ(static_cast<uint32_t>(camera.set_attribute(CameraAttributes::ISO,100)),SCE_CAMERA_ERROR_ATTRIBUTE_UNKNOWN);
}

TEST(GuestCameraLifecycle, read_before_start_does_not_fabricate_a_frame) {
    Camera camera;
    SceCameraRead read{};
    std::array<unsigned char,16> output{}; output.fill(0xCD);
    EXPECT_EQ(static_cast<uint32_t>(camera.read(&read,output.data(),nullptr,nullptr,output.size(),0,0)),SCE_CAMERA_ERROR_NOT_START);
    EXPECT_EQ(read.status,SCE_CAMERA_STATUS_IS_NOT_ACTIVE);
    EXPECT_EQ(camera.is_active(),0);
    for (const auto value : output) EXPECT_EQ(value,0xCD);
}

namespace {
class GuestCameraConversion : public ::testing::Test {
protected:
    // Four 2x2 chroma blocks: black, white, red, blue. Padding is deliberately noisy.
    std::array<unsigned char, 48> bytes{};
    SDL_Surface *source = nullptr;
    using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
    void SetUp() override {
        bytes.fill(0xCD);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                bytes[y * 8 + x] = y < 2 ? (x < 2 ? 16 : 235) : (x < 2 ? 81 : 41);
        const unsigned char chroma[] = {128,128,128,128,90,240,240,110};
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 4; ++x) bytes[32 + y * 8 + x] = chroma[y * 4 + x];
        source = SDL_CreateSurfaceFrom(4,4,SDL_PIXELFORMAT_NV12,bytes.data(),8);
        ASSERT_NE(source,nullptr);
        ASSERT_TRUE(SDL_SetSurfaceColorspace(source,SDL_COLORSPACE_BT601_LIMITED));
    }
    void TearDown() override { SDL_DestroySurface(source); }
    Surface convert(SDL_PixelFormat format, SDL_Colorspace colorspace, int size = 4) {
        return Surface(ecv_camera_convert_surface(source,format,colorspace,size,size),SDL_DestroySurface);
    }
    void expect_rgb_scene(SDL_Surface *surface) {
        Uint8 r=0,g=0,b=0,a=0;
        ASSERT_TRUE(SDL_ReadSurfacePixel(surface,0,0,&r,&g,&b,&a));
        EXPECT_LE(r,2);EXPECT_LE(g,2);EXPECT_LE(b,2);EXPECT_EQ(a,255);
        ASSERT_TRUE(SDL_ReadSurfacePixel(surface,surface->w-1,0,&r,&g,&b,&a));
        EXPECT_GE(r,253);EXPECT_GE(g,253);EXPECT_GE(b,253);EXPECT_EQ(a,255);
        ASSERT_TRUE(SDL_ReadSurfacePixel(surface,0,surface->h-1,&r,&g,&b,&a));
        EXPECT_GT(static_cast<int>(r),static_cast<int>(g)+100);
        EXPECT_GT(static_cast<int>(r),static_cast<int>(b)+100);EXPECT_EQ(a,255);
        ASSERT_TRUE(SDL_ReadSurfacePixel(surface,surface->w-1,surface->h-1,&r,&g,&b,&a));
        EXPECT_GT(static_cast<int>(b),static_cast<int>(r)+100);
        EXPECT_GT(static_cast<int>(b),static_cast<int>(g)+100);EXPECT_EQ(a,255);
    }
};
}

TEST_F(GuestCameraConversion, limited_nv12_to_abgr_has_opaque_correct_channels) {
    auto output=convert(SDL_PIXELFORMAT_ABGR8888,SDL_COLORSPACE_RGB_DEFAULT);
    ASSERT_NE(output,nullptr) << SDL_GetError();
    EXPECT_EQ(output->format,SDL_PIXELFORMAT_ABGR8888);
    expect_rgb_scene(output.get());
    const auto *red=static_cast<const unsigned char *>(output->pixels)+3*output->pitch;
    EXPECT_GT(static_cast<int>(red[0]),static_cast<int>(red[2])+100);EXPECT_EQ(red[3],255);
}

TEST_F(GuestCameraConversion, limited_nv12_to_argb_has_opaque_correct_channels) {
    auto output=convert(SDL_PIXELFORMAT_ARGB8888,SDL_COLORSPACE_RGB_DEFAULT);
    ASSERT_NE(output,nullptr) << SDL_GetError();
    EXPECT_EQ(output->format,SDL_PIXELFORMAT_ARGB8888);
    expect_rgb_scene(output.get());
    const auto *red=static_cast<const unsigned char *>(output->pixels)+3*output->pitch;
    EXPECT_GT(static_cast<int>(red[2]),static_cast<int>(red[0])+100);EXPECT_EQ(red[3],255);
}

TEST_F(GuestCameraConversion, rgb_resize_keeps_scene_and_alpha) {
    for (int size : {2,8}) {
        auto output=convert(SDL_PIXELFORMAT_ABGR8888,SDL_COLORSPACE_RGB_DEFAULT,size);
        ASSERT_NE(output,nullptr) << SDL_GetError();
        EXPECT_EQ(output->w,size);EXPECT_EQ(output->h,size);expect_rgb_scene(output.get());
    }
}

TEST_F(GuestCameraConversion, yv12_full_and_limited_range_change_luma) {
    auto full=convert(SDL_PIXELFORMAT_YV12,SDL_COLORSPACE_BT601_FULL);
    auto limited=convert(SDL_PIXELFORMAT_YV12,SDL_COLORSPACE_BT601_LIMITED);
    ASSERT_NE(full,nullptr) << SDL_GetError();ASSERT_NE(limited,nullptr) << SDL_GetError();
    const auto *f=static_cast<const unsigned char *>(full->pixels);
    const auto *l=static_cast<const unsigned char *>(limited->pixels);
    EXPECT_NEAR(f[0],0,1);EXPECT_NEAR(f[3],255,1);
    EXPECT_EQ(l[0],16);EXPECT_EQ(l[3],235);
}

TEST_F(GuestCameraConversion, yuy2_full_and_limited_range_change_luma) {
    auto full=convert(SDL_PIXELFORMAT_YUY2,SDL_COLORSPACE_BT601_FULL);
    auto limited=convert(SDL_PIXELFORMAT_YUY2,SDL_COLORSPACE_BT601_LIMITED);
    ASSERT_NE(full,nullptr) << SDL_GetError();ASSERT_NE(limited,nullptr) << SDL_GetError();
    const auto *f=static_cast<const unsigned char *>(full->pixels);
    const auto *l=static_cast<const unsigned char *>(limited->pixels);
    EXPECT_NEAR(f[0],0,1);EXPECT_NEAR(f[6],255,1);
    EXPECT_EQ(l[0],16);EXPECT_EQ(l[6],235);
}

TEST_F(GuestCameraConversion, same_space_yuv_preserves_input_padding_and_active_values) {
    const auto before=bytes;
    auto output=convert(SDL_PIXELFORMAT_YV12,SDL_COLORSPACE_BT601_LIMITED);
    ASSERT_NE(output,nullptr) << SDL_GetError();
    EXPECT_EQ(bytes,before);
    const auto *y=static_cast<const unsigned char *>(output->pixels);
    EXPECT_EQ(y[0],16);EXPECT_EQ(y[3],235);
    EXPECT_EQ(y[3*output->pitch],81);EXPECT_EQ(y[3*output->pitch+3],41);
    const int cp=(output->pitch+1)/2;
    const auto *v=y+output->pitch*4;
    const auto *u=v+cp*2;
    EXPECT_EQ(v[cp],240);EXPECT_EQ(u[cp],90);
}

TEST_F(GuestCameraConversion, full_range_nv12_decodes_black_white_and_opaque_alpha) {
    for (int y=0;y<4;++y) for (int x=0;x<4;++x)
        bytes[y*8+x]=y<2?(x<2?0:255):(x<2?76:29);
    bytes[40]=85;bytes[41]=255;bytes[42]=255;bytes[43]=107;
    ASSERT_TRUE(SDL_SetSurfaceColorspace(source,SDL_COLORSPACE_BT601_FULL));
    for (auto format : {SDL_PIXELFORMAT_ABGR8888,SDL_PIXELFORMAT_ARGB8888}) {
        auto output=convert(format,SDL_COLORSPACE_RGB_DEFAULT);
        ASSERT_NE(output,nullptr) << SDL_GetError();expect_rgb_scene(output.get());
    }
}

TEST_F(GuestCameraConversion, invalid_input_returns_no_surface_without_touching_source) {
    const auto before=bytes;
    EXPECT_EQ(ecv_camera_convert_surface(nullptr,SDL_PIXELFORMAT_ABGR8888,SDL_COLORSPACE_RGB_DEFAULT,4,4),nullptr);
    EXPECT_EQ(ecv_camera_convert_surface(source,SDL_PIXELFORMAT_ABGR8888,SDL_COLORSPACE_RGB_DEFAULT,0,4),nullptr);
    EXPECT_EQ(ecv_camera_convert_surface(source,SDL_PIXELFORMAT_ABGR8888,SDL_COLORSPACE_RGB_DEFAULT,4,-1),nullptr);
    EXPECT_EQ(bytes,before);
}
