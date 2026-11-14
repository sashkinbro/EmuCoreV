// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include "config/state.h"

#include <array>
#include <camera/camera.h>
#include <camera/state.h>
#include <camera/frame_copy.h>
#include <camera/frame_convert.h>
#include <rtc/rtc.h>
#include <stb_image.h>
#include <util/log.h>

#include <SDL3/SDL_camera.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>

#if defined(__ANDROID__) && !defined(NDEBUG)
#include <SDL3/SDL_log.h>

static int ECV_CameraDiagStats(const unsigned char *data, size_t length, int width, int height, int row_stride, int pixel_stride, unsigned long long out[6])
{
    if (!data || !out || !length || width <= 0 || height <= 0 || row_stride <= 0 || pixel_stride <= 0)
        return 0;
    if ((size_t)(width - 1) > (length - 1) / (size_t)pixel_stride)
        return 0;
    const size_t row_bytes = (size_t)(width - 1) * pixel_stride + 1;
    if (row_bytes > (size_t)row_stride || (size_t)(height - 1) > (length - row_bytes) / (size_t)row_stride)
        return 0;
    unsigned int hash = 2166136261u;
    unsigned long long lo = 255, hi = 0, sum = 0, count = 0, nonzero = 0;
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            const unsigned int value = data[(size_t)row * row_stride + (size_t)col * pixel_stride];
            if (value < lo) lo = value;
            if (value > hi) hi = value;
            sum += value; ++count; nonzero += value != 0;
            hash = (hash ^ value) * 16777619u;
        }
    }
    out[0] = lo; out[1] = hi; out[2] = sum; out[3] = count; out[4] = nonzero; out[5] = hash;
    return 1;
}

static void ecv_camera_diag_plane(const char *stage, unsigned int ordinal, int plane,
    const unsigned char *data, size_t capacity, int width, int height, int stride, int pixel_stride = 1) {
    unsigned long long stats[6];
    if (ECV_CameraDiagStats(data, capacity, width, height, stride, pixel_stride, stats))
        SDL_Log("ECV_CAMERA_DIAG %s frame=%u plane=%d stride=%d min=%llu max=%llu mean=%llu nonzero=%llu count=%llu checksum=%llx", stage, ordinal, plane, stride, stats[0], stats[1], stats[2] / stats[3], stats[4], stats[3], stats[5]);
    else SDL_Log("ECV_CAMERA_DIAG %s frame=%u plane=%d aggregate rejected capacity=%llu size=%dx%d stride=%d pixel=%d", stage, ordinal, plane, (unsigned long long)capacity, width, height, stride, pixel_stride);
}

static void ecv_camera_diag_surface(const char *stage, unsigned int ordinal, SDL_Surface *surface) {
    if (!surface) { SDL_Log("ECV_CAMERA_DIAG %s frame=%u null surface: %s", stage, ordinal, SDL_GetError()); return; }
    SDL_Log("ECV_CAMERA_DIAG %s frame=%u format=%s size=%dx%d pitch=%d colorspace=%u", stage, ordinal, SDL_GetPixelFormatName(surface->format), surface->w, surface->h, surface->pitch, (unsigned int)SDL_GetSurfaceColorspace(surface));
    if (!surface->pixels || surface->w <= 0 || surface->h <= 0 || surface->pitch <= 0) return;
    const auto *pixels = static_cast<const unsigned char *>(surface->pixels);
    const size_t luma_bytes = static_cast<size_t>(surface->pitch) * surface->h;
    if (surface->format == SDL_PIXELFORMAT_NV12 || surface->format == SDL_PIXELFORMAT_YV12) {
        if ((surface->w & 1) || (surface->h & 1) || surface->pitch < surface->w) return;
        ecv_camera_diag_plane(stage, ordinal, 0, pixels, luma_bytes, surface->w, surface->h, surface->pitch);
        if (surface->format == SDL_PIXELFORMAT_NV12) {
            const size_t chroma_bytes = static_cast<size_t>(surface->pitch) * (surface->h / 2);
            ecv_camera_diag_plane(stage, ordinal, 1, pixels + luma_bytes, chroma_bytes, surface->w / 2, surface->h / 2, surface->pitch, 2);
            if (chroma_bytes) ecv_camera_diag_plane(stage, ordinal, 2, pixels + luma_bytes + 1, chroma_bytes - 1, surface->w / 2, surface->h / 2, surface->pitch, 2);
        } else {
            const int chroma_pitch = (surface->pitch + 1) / 2;
            const size_t chroma_bytes = static_cast<size_t>(chroma_pitch) * (surface->h / 2);
            ecv_camera_diag_plane(stage, ordinal, 2, pixels + luma_bytes, chroma_bytes, surface->w / 2, surface->h / 2, chroma_pitch);
            ecv_camera_diag_plane(stage, ordinal, 1, pixels + luma_bytes + chroma_bytes, chroma_bytes, surface->w / 2, surface->h / 2, chroma_pitch);
        }
    } else if (!SDL_ISPIXELFORMAT_FOURCC(surface->format) || surface->format == SDL_PIXELFORMAT_YUY2) {
        const int bytes = surface->format == SDL_PIXELFORMAT_YUY2 ? 2 : SDL_BYTESPERPIXEL(surface->format);
        if (bytes > 0 && surface->w <= surface->pitch / bytes)
            ecv_camera_diag_plane(stage, ordinal, 0, pixels, luma_bytes, surface->w * bytes, surface->h, surface->pitch);
    }
}
#endif

struct stbi_deleter {
    void operator()(stbi_uc *d) const { stbi_image_free(d); }
};

struct SDL_SurfaceDeleter {
    void operator()(SDL_Surface *s) const { SDL_DestroySurface(s); }
};

struct SDL_CameraDeleter {
    void operator()(SDL_Camera *c) const { SDL_CloseCamera(c); }
};

struct SDL_Deleter {
    void operator()(void *c) const { SDL_free(c); }
};

using SDL_SurfacePtr = std::unique_ptr<SDL_Surface, SDL_SurfaceDeleter>;
using stbi_uc_ptr = std::unique_ptr<stbi_uc, stbi_deleter>;
using SDL_CameraPtr = std::unique_ptr<SDL_Camera, SDL_CameraDeleter>;
using SDL_CameraListPtr = std::unique_ptr<SDL_CameraID[], SDL_Deleter>;

class Camera::CameraImpl {
public:
#if defined(__ANDROID__) && !defined(NDEBUG)
    unsigned int diag_frames = 0;
#endif
    SDL_CameraPtr sdl_camera;
    SDL_CameraID device_id{};
    bool suspended = false;
    int reverse = SCE_CAMERA_REVERSE_OFF;
    SDL_SurfacePtr frame;
    SDL_PixelFormat format;
    SDL_Colorspace colorspace;
    std::mutex frame_mutex;
};

Camera::Camera()
    : pImpl(std::make_unique<CameraImpl>()) {}
Camera::~Camera() = default;

static bool init_web_camera(Camera *self) {
    // open web camera via SDL3
    int num_cameras;
    SDL_CameraListPtr cameras(SDL_GetCameras(&num_cameras));
    if (!cameras) {
        LOG_ERROR("SDL_GetCameras failed. Error: {}", SDL_GetError());
        return false;
    }
    self->pImpl->device_id = 0;
    for (int i = 0; i < num_cameras; i++) {
        auto camera_name = SDL_GetCameraName(cameras[i]);
        if (camera_name && self->id == camera_name) {
            if (self->pImpl->device_id != 0) {
                LOG_ERROR("Ambiguous camera name '{}'; refusing to select a different sensor", self->id);
                self->pImpl->device_id = 0;
                return false;
            }
            self->pImpl->device_id = cameras[i];
        }
    }
    if (self->pImpl->device_id == 0) {
        LOG_ERROR("Web camera with id '{}' not found.", self->id);
        return false;
    }
    uint16_t framerate_numerator = self->info.framerate;
    uint16_t framerate_denominator = 1;
    if (self->info.framerate == SCE_CAMERA_FRAMERATE_3_FPS) {
        framerate_numerator = 375;
        framerate_denominator = 100;
    } else if (self->info.framerate == SCE_CAMERA_FRAMERATE_7_FPS) {
        framerate_numerator = 75;
        framerate_denominator = 10;
    }
    self->frame_interval_us = SDL_US_PER_SECOND * framerate_denominator / framerate_numerator;
    SDL_CameraSpec camera_spec{
        .format = self->pImpl->format, /**< Frame format */
        .colorspace = self->pImpl->colorspace, /**< Frame colorspace */
        .width = self->info.width, /**< Frame width */
        .height = self->info.height, /**< Frame height */
        .framerate_numerator = framerate_numerator, /**< Frame rate numerator ((num / denom) == FPS, (denom / num) == duration in seconds) */
        .framerate_denominator = framerate_denominator /**< Frame rate demoninator ((num / denom) == FPS, (denom / num) == duration in seconds) */
    };
    int format_count = 0;
    auto **formats = SDL_GetCameraSupportedFormats(self->pImpl->device_id, &format_count);
    SDL_CameraSpec capture_spec = camera_spec;
    int64_t best_score = INT64_MAX;
    for (int i = 0; formats && i < format_count; ++i) {
        const auto &f = *formats[i];
        LOG_INFO("Camera '{}' supports {}x{} {} {}/{} FPS", self->id, f.width, f.height,
            SDL_GetPixelFormatName(f.format), f.framerate_numerator, f.framerate_denominator);
        const int64_t size_distance = std::abs(f.width - self->info.width) + std::abs(f.height - self->info.height);
        const int64_t fps_distance = f.framerate_denominator > 0
            ? std::abs(int64_t(f.framerate_numerator) * framerate_denominator - int64_t(framerate_numerator) * f.framerate_denominator) / f.framerate_denominator : 1000;
        const int64_t score = (f.format == SDL_PIXELFORMAT_NV12 ? 0 : 1000000000000LL) + size_distance * 1000000 + fps_distance;
        if (score < best_score) { best_score = score; capture_spec = f; }
    }
    SDL_free(formats);
    // Open an exact native mode. Guest resizing/range conversion is explicit below,
    // avoiding SDL's generic camera scaler silently failing on FOURCC surfaces.
    self->pImpl->sdl_camera.reset(SDL_OpenCamera(self->pImpl->device_id, &capture_spec));
    if (!self->pImpl->sdl_camera) {
        LOG_ERROR("Failed to open camera: {}", SDL_GetError());
        return false;
    }
    return true;
}

static SDL_SurfacePtr convert_camera_frame(Camera *self, SDL_Surface *source) {
    return SDL_SurfacePtr(ecv_camera_convert_surface(source, self->pImpl->format,
        self->pImpl->colorspace, self->info.width, self->info.height));
}

static bool init_image(Camera *self) {
    FILE *f = FOPEN(fs_utils::utf8_to_path(self->image).c_str(), "rb");
    if (!f) {
        LOG_ERROR("Failed to open camera image file: {} Error: {}", self->image, strerror(errno));
        return false;
    }

    int32_t width = 0, height = 0;
    stbi_uc_ptr data(stbi_load_from_file(f, &width, &height, nullptr, STBI_rgb));
    fclose(f);
    if (!data) {
        LOG_ERROR("Failed to load camera image from {}: {}", self->image, stbi_failure_reason());
        return false;
    }

    SDL_SurfacePtr image_surface(SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGB24, data.get(), width * SDL_BYTESPERPIXEL(SDL_PIXELFORMAT_RGB24)));
    if (!image_surface) {
        LOG_ERROR("Failed to create SDL surface from image data: {}", SDL_GetError());
        return false;
    }

    if (width != self->info.width || height != self->info.height) {
        image_surface.reset(SDL_ScaleSurface(image_surface.get(), self->info.width, self->info.height, SDL_SCALEMODE_LINEAR));
        if (!image_surface) {
            LOG_ERROR("Failed to scale SDL surface: {}", SDL_GetError());
            return false;
        }
    }

    image_surface.reset(SDL_ConvertSurfaceAndColorspace(image_surface.get(), self->pImpl->format, nullptr, self->pImpl->colorspace, 0));
    if (!image_surface) {
        LOG_ERROR("Failed to convert SDL surface: {}", SDL_GetError());
        return false;
    }

    // If frame does not own its data then duplicate it
    if (image_surface->flags & SDL_SURFACE_PREALLOCATED) {
        image_surface.reset(SDL_DuplicateSurface(image_surface.get()));
        if (!image_surface) {
            LOG_ERROR("Failed to duplicate SDL surface: {}", SDL_GetError());
            return false;
        }
    }
    self->pImpl->frame = std::move(image_surface);

    return true;
}

static bool init_solid_color(Camera *self) {
    SDL_SurfacePtr image_surface(SDL_CreateSurface(self->info.width, self->info.height, SDL_PIXELFORMAT_ARGB8888));
    if (!image_surface) {
        LOG_ERROR("Failed to create SDL surface for solid color: {}", SDL_GetError());
        return false;
    }
    if (!SDL_FillSurfaceRect(image_surface.get(), nullptr, self->color)) {
        LOG_ERROR("Failed to fill SDL surface with solid color: {}", SDL_GetError());
        return false;
    }
    SDL_SurfacePtr converted_surface(SDL_ConvertSurfaceAndColorspace(image_surface.get(), self->pImpl->format, nullptr, self->pImpl->colorspace, 0));
    if (!converted_surface) {
        LOG_ERROR("Failed to convert SDL surface for solid color: {}", SDL_GetError());
        return false;
    }
    self->pImpl->frame = std::move(converted_surface);
    return true;
}

static bool refresh_camera_image(Camera *self) {
    self->pImpl->sdl_camera.reset();
    self->pImpl->frame.reset();
    switch (self->type) {
    case CameraType::WebCamera: return init_web_camera(self);
    case CameraType::Image: return init_image(self);
    case CameraType::SolidColor: return init_solid_color(self);
    }
    return false;
}

int Camera::get_attribute(CameraAttributes attribute) {
    const std::lock_guard lock(pImpl->frame_mutex);
    switch (attribute) {
    case CameraAttributes::Saturation: return SCE_CAMERA_SATURATION_10;
    case CameraAttributes::Brightness: return 128; // 0-255
    case CameraAttributes::Contrast: return 128; // random default value  0-255
    case CameraAttributes::Sharpness: return SCE_CAMERA_SHARPNESS_100;
    case CameraAttributes::Reverse: return pImpl->reverse;
    case CameraAttributes::Effect: return SCE_CAMERA_EFFECT_NORMAL;
    case CameraAttributes::EV: return 0;
    case CameraAttributes::Zoom: return 10; // 10-40
    case CameraAttributes::AntiFlicker: return SCE_CAMERA_ANTIFLICKER_AUTO;
    case CameraAttributes::ISO: return SCE_CAMERA_ISO_AUTO;
    case CameraAttributes::Gain: return SCE_CAMERA_GAIN_AUTO;
    case CameraAttributes::WhiteBalance: return SCE_CAMERA_WB_AUTO;
    case CameraAttributes::Backlight: return SCE_CAMERA_BACKLIGHT_OFF;
    case CameraAttributes::Nightmode: return SCE_CAMERA_NIGHTMODE_OFF;
    case CameraAttributes::ExposureCeiling: return 0;
    case CameraAttributes::AutoControlHold: return 0;
    // undescribed parameters
    case CameraAttributes::ImageQuality: return 0;
    case CameraAttributes::NoiseReduction: return 0;
    case CameraAttributes::SharpnessOff: return 0;
    }
    return 0;
}

int Camera::set_attribute(CameraAttributes attribute, int value) {
    const std::lock_guard lock(pImpl->frame_mutex);
    if (attribute == CameraAttributes::Reverse) {
        if (value < SCE_CAMERA_REVERSE_OFF || value > SCE_CAMERA_REVERSE_MIRROR_FLIP)
            return SCE_CAMERA_ERROR_PARAM;
        pImpl->reverse = value;
        return 0;
    }
    // Never claim a host camera control changed when it was not implemented.
    return SCE_CAMERA_ERROR_ATTRIBUTE_UNKNOWN;
}

int Camera::open(SceCameraInfo *info, uint64_t base_tick, uint64_t start_tick) {
    const std::lock_guard lock(pImpl->frame_mutex);
    if (info->format == SCE_CAMERA_FORMAT_RAW8)
        return SCE_CAMERA_ERROR_FORMAT_UNKNOWN;
    tick_diff_us = rtc_get_ticks(base_tick) - start_tick - (SDL_GetTicksNS() / SDL_NS_PER_US);
    this->is_opened = true;
    this->is_started = false;
    this->frame_idx = 0;
    this->last_frame_timestamp_us = 0;
    this->next_frame_timestamp_us = 0;
    pImpl->reverse = SCE_CAMERA_REVERSE_OFF;
    this->info = {};
    memcpy(&(this->info), info, std::min<size_t>(sizeof(SceCameraInfo), info->size));
    switch (this->info.format) {
    case SCE_CAMERA_FORMAT_YUV420_PLANE: // YUV 4:2:0 planar
        this->pImpl->format = SDL_PIXELFORMAT_YV12;
        this->pImpl->colorspace = SDL_COLORSPACE_BT601_FULL;
        break;
    case SCE_CAMERA_FORMAT_YUV422_PLANE: // YUV 4:2:2 planar
        this->pImpl->format = SDL_PIXELFORMAT_YUY2; // use packed format cause SDL does not have YUV 4:2:2 planar
        this->pImpl->colorspace = SDL_COLORSPACE_BT601_FULL;
        break;
    case SCE_CAMERA_FORMAT_YUV422_PACKED: // YUV 4:2:2 packed
        this->pImpl->format = SDL_PIXELFORMAT_YUY2;
        this->pImpl->colorspace = SDL_COLORSPACE_BT601_FULL;
        break;
    case SCE_CAMERA_FORMAT_ARGB: // ARGB format
        this->pImpl->format = SDL_PIXELFORMAT_ARGB8888;
        this->pImpl->colorspace = SDL_COLORSPACE_RGB_DEFAULT;
        break;
    case SCE_CAMERA_FORMAT_ABGR: // ABGR format
        this->pImpl->format = SDL_PIXELFORMAT_ABGR8888;
        this->pImpl->colorspace = SDL_COLORSPACE_RGB_DEFAULT;
        break;
    case SCE_CAMERA_FORMAT_RAW8: // unsupported, use default
    default:
        this->pImpl->format = SDL_PIXELFORMAT_ARGB8888; // ABGR format
        this->pImpl->colorspace = SDL_COLORSPACE_RGB_DEFAULT;
    }

    if (this->info.range == SCE_CAMERA_DATA_RANGE_BT601 && this->info.format <= SCE_CAMERA_FORMAT_YUV420_PLANE) {
        this->pImpl->colorspace = SDL_COLORSPACE_BT601_LIMITED;
    }

    return 0;
}

int Camera::start() {
    const std::lock_guard lock(pImpl->frame_mutex);
    if (pImpl->suspended)
        return SCE_CAMERA_ERROR_NOT_ACTIVE;
    const uint64_t numerator = info.framerate == SCE_CAMERA_FRAMERATE_3_FPS ? 375
        : info.framerate == SCE_CAMERA_FRAMERATE_7_FPS ? 750 : info.framerate * 100;
    frame_interval_us = SDL_US_PER_SECOND * 100 / numerator;
    if (!refresh_camera_image(this))
        return SCE_CAMERA_ERROR_NOT_MOUNTED;
    is_started = true; // A capture request; IsActive stays zero while permission is pending.
    frame_idx = 0;
    last_frame_timestamp_us = 0;
    next_frame_timestamp_us = 0;
    return 0;
}

static bool camera_disconnected(Camera *self) {
    return self->pImpl->sdl_camera && SDL_GetBooleanProperty(
        SDL_GetCameraProperties(self->pImpl->sdl_camera.get()), "emucorev.camera.disconnected", false);
}

int Camera::is_active() {
    const std::lock_guard lock(pImpl->frame_mutex);
    if (!is_started || pImpl->suspended) return 0;
    if (type != CameraType::WebCamera) return pImpl->frame ? 1 : 0;
    return pImpl->sdl_camera && !camera_disconnected(this)
        && SDL_GetCameraPermissionState(pImpl->sdl_camera.get()) > 0 ? 1 : 0;
}

int Camera::read(SceCameraRead *read, void *pIBase, void *pUBase, void *pVBase, SceSize sizeIBase, SceSize sizeUBase, SceSize sizeVBase) {
    std::unique_lock lock(pImpl->frame_mutex);
    read->frame = frame_idx;
    read->timestamp = last_frame_timestamp_us;
    read->status = SCE_CAMERA_STATUS_IS_NOT_ACTIVE;
    if (!is_started) return SCE_CAMERA_ERROR_NOT_START;
    if (pImpl->suspended) {
        read->status = SCE_CAMERA_STATUS_IS_FORCED_STOP;
        return SCE_CAMERA_ERROR_NOT_ACTIVE;
    }
    if (read->mode != SCE_CAMERA_READ_MODE_WAIT_NEXTFRAME_ON && read->mode != SCE_CAMERA_READ_MODE_WAIT_NEXTFRAME_OFF)
        return SCE_CAMERA_ERROR_PARAM;
    const auto deadline = SDL_GetTicksNS() + SDL_NS_PER_SECOND;
    const auto now_us = SDL_GetTicksNS() / SDL_NS_PER_US;
    if (next_frame_timestamp_us && now_us < next_frame_timestamp_us) {
        if (read->mode == SCE_CAMERA_READ_MODE_WAIT_NEXTFRAME_OFF) {
            read->status = SCE_CAMERA_STATUS_IS_ALREADY_READ;
            return 0;
        }
        const auto delay_ns = (next_frame_timestamp_us - now_us) * SDL_NS_PER_US;
        lock.unlock();
        SDL_DelayNS(delay_ns);
        lock.lock();
        if (!is_started) return SCE_CAMERA_ERROR_NOT_START;
        if (pImpl->suspended) return SCE_CAMERA_ERROR_NOT_ACTIVE;
    }
#if defined(__ANDROID__) && !defined(NDEBUG)
    unsigned int diag_frame = 0;
    bool diag_sample = false;
#endif
    Uint64 timestamp_ns = 0;
    if (type == CameraType::WebCamera) {
        for (;;) {
            if (!pImpl->sdl_camera || camera_disconnected(this))
                return SCE_CAMERA_ERROR_NOT_MOUNTED;
            const int permission = SDL_GetCameraPermissionState(pImpl->sdl_camera.get());
            if (permission < 0) return SCE_CAMERA_ERROR_NOT_MOUNTED;
            if (permission == 0) {
                read->status = SCE_CAMERA_STATUS_IS_NOT_STABLE;
                return SCE_CAMERA_ERROR_NOT_ACTIVE;
            }
            SDL_Surface *surface = SDL_AcquireCameraFrame(pImpl->sdl_camera.get(), &timestamp_ns);
            if (surface) {
                // Drain the small FIFO, returning only a genuine newly acquired frame.
                for (int i = 0; i < 16; ++i) {
                    Uint64 next_timestamp = 0;
                    auto *next = SDL_AcquireCameraFrame(pImpl->sdl_camera.get(), &next_timestamp);
                    if (!next) break;
                    SDL_ReleaseCameraFrame(pImpl->sdl_camera.get(), surface);
                    surface = next;
                    timestamp_ns = next_timestamp;
                }
                if (camera_disconnected(this)) {
                    SDL_ReleaseCameraFrame(pImpl->sdl_camera.get(), surface);
                    return SCE_CAMERA_ERROR_NOT_MOUNTED;
                }
#if defined(__ANDROID__) && !defined(NDEBUG)
                diag_frame = pImpl->diag_frames < 121 ? ++pImpl->diag_frames : 121;
                diag_sample = diag_frame <= 3 || diag_frame == 30 || diag_frame == 60 || diag_frame == 120;
                if (diag_sample) ecv_camera_diag_surface("SDL acquired", diag_frame, surface);
#endif
                pImpl->frame = convert_camera_frame(this, surface);
#if defined(__ANDROID__) && !defined(NDEBUG)
                if (diag_sample) ecv_camera_diag_surface("converted", diag_frame, pImpl->frame.get());
#endif
                SDL_ReleaseCameraFrame(pImpl->sdl_camera.get(), surface);
                if (!pImpl->frame) return SCE_CAMERA_ERROR_FATAL;
                break;
            }
            if (read->mode == SCE_CAMERA_READ_MODE_WAIT_NEXTFRAME_OFF) {
                read->status = frame_idx ? SCE_CAMERA_STATUS_IS_ALREADY_READ : SCE_CAMERA_STATUS_IS_NOT_STABLE;
                return frame_idx ? 0 : SCE_CAMERA_ERROR_NOT_ACTIVE;
            }
            if (SDL_GetTicksNS() >= deadline) return SCE_CAMERA_ERROR_TIMEOUT;
            lock.unlock();
            SDL_Delay(1);
            lock.lock();
            if (!is_started) return SCE_CAMERA_ERROR_NOT_START;
            if (pImpl->suspended) return SCE_CAMERA_ERROR_NOT_ACTIVE;
        }
    } else {
        // Explicit desktop image/solid-color sources remain available; never fallback to them.
        const auto now = SDL_GetTicksNS() / SDL_NS_PER_US;
        if (next_frame_timestamp_us && now < next_frame_timestamp_us) {
            if (read->mode == SCE_CAMERA_READ_MODE_WAIT_NEXTFRAME_OFF) {
                read->status = SCE_CAMERA_STATUS_IS_ALREADY_READ;
                return 0;
            }
            const auto delay = next_frame_timestamp_us - now;
            lock.unlock();
            SDL_DelayNS(delay * SDL_NS_PER_US);
            lock.lock();
            if (!is_started) return SCE_CAMERA_ERROR_NOT_START;
        }
        timestamp_ns = SDL_GetTicksNS();
        next_frame_timestamp_us = timestamp_ns / SDL_NS_PER_US + frame_interval_us;
    }
    if (!pImpl->frame || pImpl->frame->w != info.width || pImpl->frame->h != info.height)
        return SCE_CAMERA_ERROR_FATAL;
    auto *frame = pImpl->frame.get();
    const size_t source_size = info.format == SCE_CAMERA_FORMAT_YUV420_PLANE
        ? static_cast<size_t>(frame->pitch) * frame->h + static_cast<size_t>((frame->pitch + 1) / 2) * frame->h
        : static_cast<size_t>(frame->pitch) * frame->h;
    if (!SDL_LockSurface(frame)) return SCE_CAMERA_ERROR_FATAL;
    const bool copied = ecv_camera_copy(info.format, info.width, info.height, info.pitch, pImpl->reverse,
        static_cast<const unsigned char *>(frame->pixels), frame->pitch, source_size,
        static_cast<unsigned char *>(pIBase), sizeIBase, static_cast<unsigned char *>(pUBase), sizeUBase,
        static_cast<unsigned char *>(pVBase), sizeVBase);
    SDL_UnlockSurface(frame);
#if defined(__ANDROID__) && !defined(NDEBUG)
    if (diag_sample) {
        SDL_Log("ECV_CAMERA_DIAG guest copy frame=%u copied=%d format=%u size=%ux%u padding=%u capacity=%u/%u/%u", diag_frame, copied, (unsigned int)info.format, info.width, info.height, info.pitch, sizeIBase, sizeUBase, sizeVBase);
        if (copied) {
            const int bytes = info.format == SCE_CAMERA_FORMAT_ARGB || info.format == SCE_CAMERA_FORMAT_ABGR ? 4 : (info.format == SCE_CAMERA_FORMAT_YUV422_PACKED ? 2 : 1);
            ecv_camera_diag_plane("guest written", diag_frame, 0, static_cast<const unsigned char *>(pIBase), sizeIBase, info.width * bytes, info.height, info.width * bytes + info.pitch);
            if (info.format == SCE_CAMERA_FORMAT_YUV420_PLANE || info.format == SCE_CAMERA_FORMAT_YUV422_PLANE) {
                const int height = info.format == SCE_CAMERA_FORMAT_YUV420_PLANE ? info.height / 2 : info.height;
                const int pitch = (info.width + info.pitch + 1) / 2;
                ecv_camera_diag_plane("guest written", diag_frame, 1, static_cast<const unsigned char *>(pUBase), sizeUBase, info.width / 2, height, pitch);
                ecv_camera_diag_plane("guest written", diag_frame, 2, static_cast<const unsigned char *>(pVBase), sizeVBase, info.width / 2, height, pitch);
            }
        }
    }
#endif
    if (!copied) return SCE_CAMERA_ERROR_PARAM;
    last_frame_timestamp_us = timestamp_ns / SDL_NS_PER_US + tick_diff_us;
    next_frame_timestamp_us = SDL_GetTicksNS() / SDL_NS_PER_US + frame_interval_us;
    read->frame = ++frame_idx;
    read->timestamp = last_frame_timestamp_us;
    read->status = SCE_CAMERA_STATUS_IS_ACTIVE;
    read->exposure_time = 0; // Not reported by SDL; do not fabricate sensor exposure.
    read->exposure_time_gap = 0;
    read->raw8_format = SCE_CAMERA_RAW8_FORMAT_UNKNOWN;
    return 0;
}

int Camera::close() {
    const std::lock_guard lock(pImpl->frame_mutex);
    is_started = false;
    is_opened = false;
    pImpl->frame.reset();
    pImpl->sdl_camera.reset();
    last_frame_timestamp_us = next_frame_timestamp_us = 0;
    return 0;
}

int Camera::stop() {
    const std::lock_guard lock(pImpl->frame_mutex);
    is_started = false;
    pImpl->frame.reset();
    pImpl->sdl_camera.reset();
    last_frame_timestamp_us = next_frame_timestamp_us = 0;
    return 0;
}

void Camera::set_suspended(bool suspended) {
    const std::lock_guard lock(pImpl->frame_mutex);
    if (pImpl->suspended == suspended) return;
    pImpl->suspended = suspended;
    pImpl->frame.reset();
    pImpl->sdl_camera.reset();
    last_frame_timestamp_us = next_frame_timestamp_us = 0;
    if (!suspended && is_started && !refresh_camera_image(this))
        LOG_WARN("Could not resume camera '{}'", id);
}

void Camera::update_config(int type, const std::string &id, const std::string &image, uint32_t color) {
    if (static_cast<int>(this->type) == type && this->id == id && this->image == image && this->color == color)
        return;
    this->type = static_cast<CameraType>(type);
    this->id = id;
    this->image = image;
    this->color = color;
    if (is_started && !pImpl->suspended) {
        const std::lock_guard lock(pImpl->frame_mutex);
        refresh_camera_image(this);
    }
}

void CameraState::deinit() {
    for (auto &cam : cameras) {
        if (cam.is_started)
            cam.stop();

        cam.is_opened = false;
        cam.is_started = false;
        cam.info = {};
        cam.frame_idx = 0;
        cam.last_frame_timestamp_us = 0;
        cam.next_frame_timestamp_us = 0;
        cam.frame_interval_us = 0;
        cam.tick_diff_us = 0;
    }
    active_camera = SCE_CAMERA_DEVICE_UNKNOWN;
}

BOOST_DESCRIBE_ENUM(SDL_CameraPosition, SDL_CAMERA_POSITION_UNKNOWN, SDL_CAMERA_POSITION_FRONT_FACING, SDL_CAMERA_POSITION_BACK_FACING)

void init_default_cameras(Config &cfg) {
#ifdef __ANDROID__
    // Older Android runs persisted the no-hardware solid-color fallback. Migrate
    // those selections to actual facing cameras; never expose a fake Android feed.
    if (cfg.front_camera_type != WebCamera) cfg.front_camera_id.clear();
    if (cfg.back_camera_type != WebCamera) cfg.back_camera_id.clear();
    cfg.front_camera_type = cfg.back_camera_type = WebCamera;
#endif
#ifndef __ANDROID__
    if (!((cfg.front_camera_type == WebCamera && cfg.front_camera_id.empty()) || (cfg.back_camera_type == WebCamera && cfg.back_camera_id.empty())))
        return;
#endif
    LOG_DEBUG("Initialize default cameras...");
    int num_cameras = 0;
    SDL_CameraListPtr cameras(SDL_GetCameras(&num_cameras));
    if (!cameras) return;
    std::string default_front, default_back;
    bool front_valid = false, back_valid = false;
    for (int i = 0; i < num_cameras; i++) {
        auto position = SDL_GetCameraPosition(cameras[i]);
        const char *name = SDL_GetCameraName(cameras[i]);
        if (!name) continue;
        LOG_TRACE("Found camera: name='{}' position={}", SDL_GetCameraName(cameras[i]), position);
        if (position == SDL_CAMERA_POSITION_FRONT_FACING) {
#ifdef __ANDROID__
            if (default_front.empty()) default_front = name;
            front_valid = front_valid || cfg.front_camera_id == name;
#else
            cfg.front_camera_id = SDL_GetCameraName(cameras[i]);
            cfg.front_camera_type = WebCamera;
#endif
        }
        if (position == SDL_CAMERA_POSITION_BACK_FACING) {
#ifdef __ANDROID__
            if (default_back.empty()) default_back = name;
            back_valid = back_valid || cfg.back_camera_id == name;
#else
            cfg.back_camera_id = SDL_GetCameraName(cameras[i]);
            cfg.back_camera_type = WebCamera;
#endif
        }
    }
#ifdef __ANDROID__
    // SDL names include stable Camera2 IDs. Preserve a valid selected sensor,
    // migrate legacy vendor-version names, and never assign unknown-facing USB cameras.
    if (!front_valid) cfg.front_camera_id = default_front;
    if (!back_valid) cfg.back_camera_id = default_back;
#else
    if (num_cameras > 0) {
        if (cfg.front_camera_id.empty()) cfg.front_camera_id = SDL_GetCameraName(cameras[0]);
        if (cfg.back_camera_id.empty()) cfg.back_camera_id = SDL_GetCameraName(cameras[0]);
    }
#endif
}

#if defined(__ANDROID__) && !defined(NDEBUG)
// Explicit debug Activity entry; never runs a guest title or writes image files.
extern "C" SDL_DECLSPEC int SDLCALL ECV_FrontCameraProbe(int, char **) {
    if (!SDL_InitSubSystem(SDL_INIT_CAMERA)) {
        SDL_Log("ECV_FRONT_CAMERA_PROBE FAIL init=%s", SDL_GetError());
        return 1;
    }
    struct SubsystemGuard {
        ~SubsystemGuard() { SDL_QuitSubSystem(SDL_INIT_CAMERA); }
    } subsystem_guard;
    int count = 0;
    SDL_CameraListPtr devices(SDL_GetCameras(&count));
    std::string selected;
    SDL_CameraID selected_id = 0;
    for (int i = 0; devices && i < count; ++i) {
        if (SDL_GetCameraPosition(devices[i]) == SDL_CAMERA_POSITION_FRONT_FACING) {
            const char *name = SDL_GetCameraName(devices[i]);
            if (name) { selected = name; selected_id = devices[i]; }
            break;
        }
    }
    if (selected.empty()) {
        SDL_Log("ECV_FRONT_CAMERA_PROBE FAIL no_front_sensor enumerated=%d", count);
        return 1;
    }
    SDL_Log("ECV_FRONT_CAMERA_PROBE sensor sdl_id=%u facing=FRONT name=%s", selected_id, selected.c_str());
    Camera camera;
    camera.update_config(WebCamera, selected, "", 0);
    struct CameraGuard {
        Camera &camera;
        ~CameraGuard() { camera.stop(); camera.close(); }
    } camera_guard{camera};
    SceCameraInfo info{};
    info.size = sizeof(info);
    info.format = SCE_CAMERA_FORMAT_ABGR;
    info.width = 640; info.height = 480;
    info.framerate = SCE_CAMERA_FRAMERATE_30_FPS;
    info.sizeIBase = 640 * 480 * 4;
    info.buffer = SCE_CAMERA_BUFFER_SETBYREAD;
    const int opened = camera.open(&info, 0, 0);
    const int started = opened == 0 ? camera.start() : opened;
    if (opened != 0 || started != 0) {
        SDL_Log("ECV_FRONT_CAMERA_PROBE FAIL open=0x%x start=0x%x", opened, started);
        return 1;
    }
    auto buffer = std::make_unique<unsigned char[]>(info.sizeIBase + 32);
    std::fill_n(buffer.get(), info.sizeIBase + 32, 0xCD);
    const Uint64 deadline = SDL_GetTicksNS() + 5 * SDL_NS_PER_SECOND;
    Uint64 last_timestamp = 0, last_frame = 0;
    int received = 0, last_result = 0;
    while (received < 2 && SDL_GetTicksNS() < deadline) {
        SceCameraRead read{};
        read.size = sizeof(read);
        read.mode = SCE_CAMERA_READ_MODE_WAIT_NEXTFRAME_OFF;
        last_result = camera.read(&read, buffer.get() + 16, nullptr, nullptr, info.sizeIBase, 0, 0);
        if (last_result == 0 && read.status == SCE_CAMERA_STATUS_IS_ACTIVE && read.timestamp > last_timestamp && read.frame > last_frame) {
            size_t nonblack = 0;
            unsigned int hash = 2166136261u, lo = 255, hi = 0;
            bool opaque = true, canaries = true;
            for (size_t i = 16; i < 16 + info.sizeIBase; i += 4) {
                nonblack += buffer[i] != 0 || buffer[i + 1] != 0 || buffer[i + 2] != 0;
                opaque = opaque && buffer[i + 3] == 255;
                for (int channel = 0; channel < 3; ++channel) {
                    const unsigned int value = buffer[i + channel];
                    lo = std::min(lo, value); hi = std::max(hi, value);
                    hash = (hash ^ value) * 16777619u;
                }
            }
            for (int i = 0; i < 16; ++i)
                canaries = canaries && buffer[i] == 0xCD && buffer[16 + info.sizeIBase + i] == 0xCD;
            SDL_Log("ECV_FRONT_CAMERA_PROBE frame=%llu timestamp_us=%llu size=640x480 capacity=%u nonblack=%zu rgb_min=%u rgb_max=%u checksum=%x opaque=%d canaries=%d", (unsigned long long)read.frame, (unsigned long long)read.timestamp, info.sizeIBase, nonblack, lo, hi, hash, opaque, canaries);
            if (!nonblack || !opaque || !canaries) {
                SDL_Log("ECV_FRONT_CAMERA_PROBE FAIL invalid_guest_frame");
                return 1;
            }
            last_timestamp = read.timestamp; last_frame = read.frame;
            ++received;
        }
        if (received < 2) SDL_Delay(10);
    }
    const int stopped = camera.stop(), closed = camera.close();
    const bool passed = received == 2 && stopped == 0 && closed == 0;
    SDL_Log("ECV_FRONT_CAMERA_PROBE %s frames=%d last_read=0x%x stop=0x%x close=0x%x", passed ? "PASS" : "FAIL", received, last_result, stopped, closed);
    return passed ? 0 : 1;
}
#endif
