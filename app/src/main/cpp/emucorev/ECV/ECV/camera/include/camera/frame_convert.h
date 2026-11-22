// Internal camera surface conversion shared by production and real SDL regressions.
#pragma once

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_surface.h>
#include <memory>

static SDL_Colorspace ecv_camera_rgb_colorspace(SDL_Colorspace colorspace) {
    // SDL's 8-bit YUV decoder emits gamma-encoded RGB and requires matching
    // primaries. A separate RGB conversion can then apply the primaries matrix.
    return static_cast<SDL_Colorspace>(SDL_DEFINE_COLORSPACE(SDL_COLOR_TYPE_RGB,
        SDL_COLOR_RANGE_FULL, SDL_COLORSPACEPRIMARIES(colorspace),
        SDL_TRANSFER_CHARACTERISTICS_SRGB, SDL_MATRIX_COEFFICIENTS_IDENTITY,
        SDL_CHROMA_LOCATION_NONE));
}

// Rotates an ARGB8888 surface clockwise by a multiple of 90 degrees. 90/270 swap the dimensions.
static SDL_Surface *ecv_camera_rotate_argb(SDL_Surface *src, int rotation_cw) {
    const bool swap = rotation_cw == 90 || rotation_cw == 270;
    SDL_Surface *dst = SDL_CreateSurface(swap ? src->h : src->w, swap ? src->w : src->h, SDL_PIXELFORMAT_ARGB8888);
    if (!dst) return nullptr;
    SDL_SetSurfaceColorspace(dst, SDL_GetSurfaceColorspace(src));
    const int sw = src->w, sh = src->h;
    for (int y = 0; y < sh; ++y) {
        const Uint32 *in = reinterpret_cast<const Uint32 *>(static_cast<const Uint8 *>(src->pixels) + static_cast<size_t>(y) * src->pitch);
        for (int x = 0; x < sw; ++x) {
            int dx, dy;
            switch (rotation_cw) {
            case 90: dx = sh - 1 - y; dy = x; break;
            case 180: dx = sw - 1 - x; dy = sh - 1 - y; break;
            default: dx = y; dy = sw - 1 - x; break; // 270
            }
            reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(dst->pixels) + static_cast<size_t>(dy) * dst->pitch)[dx] = in[x];
        }
    }
    return dst;
}

// rotation_cw (0/90/180/270) is applied to the decoded image before it is scaled to width x height.
static SDL_Surface *ecv_camera_convert_surface(SDL_Surface *source, SDL_PixelFormat format,
    SDL_Colorspace colorspace, int width, int height, int rotation_cw = 0) {
    if (!source || width <= 0 || height <= 0) {
        SDL_SetError("Invalid camera frame conversion input");
        return nullptr;
    }
    const SDL_Colorspace source_colorspace = SDL_GetSurfaceColorspace(source);
    if (rotation_cw == 0 && source->w == width && source->h == height && source_colorspace == colorspace)
        return SDL_ConvertSurfaceAndColorspace(source, format, nullptr, colorspace, 0);

    using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
    const SDL_Colorspace decode_colorspace = SDL_ISPIXELFORMAT_FOURCC(source->format)
        ? ecv_camera_rgb_colorspace(source_colorspace) : source_colorspace;
    Surface rgb(SDL_ConvertSurfaceAndColorspace(source, SDL_PIXELFORMAT_ARGB8888,
        nullptr, decode_colorspace, 0), SDL_DestroySurface);
    if (!rgb) return nullptr;

    if (rotation_cw != 0) {
        rgb.reset(ecv_camera_rotate_argb(rgb.get(), rotation_cw));
        if (!rgb) return nullptr;
    }

    // SDL cannot directly scale FOURCC surfaces; scale only decoded RGB pixels.
    if (rgb->w != width || rgb->h != height) {
        rgb.reset(SDL_ScaleSurface(rgb.get(), width, height, SDL_SCALEMODE_LINEAR));
        if (!rgb) return nullptr;
    }

    if (SDL_ISPIXELFORMAT_FOURCC(format)) {
        // RGB->YUV has the same primaries constraint. Convert RGB primaries
        // before encoding the requested guest matrix and full/limited range.
        const SDL_Colorspace encode_colorspace = ecv_camera_rgb_colorspace(colorspace);
        if (SDL_GetSurfaceColorspace(rgb.get()) != encode_colorspace) {
            rgb.reset(SDL_ConvertSurfaceAndColorspace(rgb.get(), SDL_PIXELFORMAT_ARGB8888,
                nullptr, encode_colorspace, 0));
            if (!rgb) return nullptr;
        }
    }
    return SDL_ConvertSurfaceAndColorspace(rgb.get(), format, nullptr, colorspace, 0);
}
