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

#include <algorithm>
#include <vector>

#include <gxm/functions.h>
#include <gxm/types.h>
#include <mem/functions.h>
#include <renderer/commands.h>
#include <renderer/driver_functions.h>
#include <renderer/functions.h>
#include <renderer/state.h>
#include <renderer/types.h>
#include <util/log.h>
#include <util/tracy.h>

#include <renderer/vulkan/state.h>
#include <renderer/vulkan/surface_cache.h>

// keywords.h must be after tracy.h for msvc compiler
#include <util/keywords.h>

extern "C" {
#include <libswscale/swscale.h>
}

namespace renderer {

// Perform a depth-to-depth sceGxmTransferCopy on the GPU (false = old behaviour i.e. transfer does nothing)
static constexpr bool use_gpu_depth_transfer = true;

template <typename T, SceGxmTransferColorKeyMode mode, SceGxmTransferType src_type, SceGxmTransferType dst_type>
static void perform_transfer_copy_impl(MemState &mem, const SceGxmTransferImage &src, const SceGxmTransferImage &dst, uint32_t key_value, uint32_t key_mask) {
    T *__restrict__ src_ptr = src.address.cast<T>().get(mem);
    T *__restrict__ dst_ptr = dst.address.cast<T>().get(mem);

    // Fast path: LINEAR→LINEAR, no color key, contiguous rows → use memmove
    // (handles overlapping src/dst correctly, which the per-element loop does not)
    if constexpr (src_type == SCE_GXM_TRANSFER_LINEAR && dst_type == SCE_GXM_TRANSFER_LINEAR && mode == SCE_GXM_TRANSFER_COLORKEY_NONE) {
        const int32_t src_stride_pixel = src.stride / sizeof(T);
        const int32_t dst_stride_pixel = dst.stride / sizeof(T);
        if (src.x == 0 && src.y == 0 && dst.x == 0 && dst.y == 0
            && src_stride_pixel == static_cast<int32_t>(src.width)
            && dst_stride_pixel == static_cast<int32_t>(dst.width)) {
            memmove(dst_ptr, src_ptr, static_cast<size_t>(src.width) * src.height * sizeof(T));
            return;
        }
        // Row-by-row memmove for strided LINEAR→LINEAR (handles overlap within each row)
        if (src.x == 0 && dst.x == 0
            && src_stride_pixel >= static_cast<int32_t>(src.width)
            && dst_stride_pixel >= static_cast<int32_t>(dst.width)) {
            const uint8_t *s = reinterpret_cast<const uint8_t *>(src_ptr) + src.y * src.stride;
            uint8_t *d = reinterpret_cast<uint8_t *>(dst_ptr) + dst.y * dst.stride;
            const size_t row_bytes = static_cast<size_t>(src.width) * sizeof(T);
            if (reinterpret_cast<uintptr_t>(d) <= reinterpret_cast<uintptr_t>(s)) {
                for (uint32_t dy = 0; dy < src.height; dy++) {
                    memmove(d + dy * dst.stride, s + dy * src.stride, row_bytes);
                }
            } else {
                for (int32_t dy = static_cast<int32_t>(src.height) - 1; dy >= 0; dy--) {
                    memmove(d + dy * dst.stride, s + dy * src.stride, row_bytes);
                }
            }
            return;
        }
    }

    auto compute_offset = [&](uint32_t dx, uint32_t dy, const SceGxmTransferImage &img, SceGxmTransferType type) -> int32_t {
        const int32_t stride_pixel = img.stride / sizeof(T);
        if (type == SCE_GXM_TRANSFER_LINEAR) {
            return dy * stride_pixel + dx;
        } else if (type == SCE_GXM_TRANSFER_TILED) {
            const uint32_t texel_offset_in_tile = ((dy % 32) * 32) + (dx % 32);
            const int32_t tile_address = (stride_pixel / 32) * (dy / 32) + (dx / 32);

            return tile_address * 1024 + texel_offset_in_tile;
        } else {
            return texture::encode_morton(dx, dy, img.width, img.height);
        }
    };

    // Check for overlap and use a snapshot of source data if needed
    const uintptr_t src_start = reinterpret_cast<uintptr_t>(src_ptr);
    const uintptr_t dst_start = reinterpret_cast<uintptr_t>(dst_ptr);
    const size_t src_span = (src.y + src.height) * (src.stride ? src.stride : src.width * sizeof(T));
    const size_t dst_span = (dst.y + dst.height) * (dst.stride ? dst.stride : dst.width * sizeof(T));
    const bool overlaps = src_start < dst_start + dst_span && dst_start < src_start + src_span;

    std::vector<T> src_copy;
    const T *safe_src = src_ptr;
    if (overlaps) {
        const size_t src_elements = src_span / sizeof(T);
        src_copy.assign(src_ptr, src_ptr + src_elements);
        safe_src = src_copy.data();
    }

    for (uint32_t dy = 0; dy < src.height; dy++) {
        for (uint32_t dx = 0; dx < src.width; dx++) {
            uint32_t src_offset = compute_offset(src.x + dx, src.y + dy, src, src_type);
            uint32_t dst_offset = compute_offset(dst.x + dx, dst.y + dy, dst, dst_type);

            T value = safe_src[src_offset];
            if constexpr (mode == SCE_GXM_TRANSFER_COLORKEY_PASS) {
                if ((value & key_mask) != key_value)
                    continue;
            } else if constexpr (mode == SCE_GXM_TRANSFER_COLORKEY_REJECT) {
                if ((value & key_mask) == key_value)
                    continue;
            }

            dst_ptr[dst_offset] = value;
        }
    }
}

template <typename T, SceGxmTransferColorKeyMode mode, SceGxmTransferType src_type>
static void perform_transfer_copy_dst_type(MemState &mem, const SceGxmTransferImage &src, const SceGxmTransferImage &dst, SceGxmTransferType dst_type, uint32_t key_value, uint32_t key_mask) {
    switch (dst_type) {
    case SCE_GXM_TRANSFER_LINEAR:
        perform_transfer_copy_impl<T, mode, src_type, SCE_GXM_TRANSFER_LINEAR>(mem, src, dst, key_value, key_mask);
        break;
    case SCE_GXM_TRANSFER_SWIZZLED:
        perform_transfer_copy_impl<T, mode, src_type, SCE_GXM_TRANSFER_SWIZZLED>(mem, src, dst, key_value, key_mask);
        break;
    case SCE_GXM_TRANSFER_TILED:
        perform_transfer_copy_impl<T, mode, src_type, SCE_GXM_TRANSFER_TILED>(mem, src, dst, key_value, key_mask);
        break;
    default:
        LOG_ERROR("Unknown transfer key mode {}", fmt::underlying(mode));
        break;
    }
}

template <typename T, SceGxmTransferColorKeyMode mode>
static void perform_transfer_copy_src_type(MemState &mem, const SceGxmTransferImage &src, const SceGxmTransferImage &dst, SceGxmTransferType src_type, SceGxmTransferType dst_type, uint32_t key_value, uint32_t key_mask) {
    switch (src_type) {
    case SCE_GXM_TRANSFER_LINEAR:
        perform_transfer_copy_dst_type<T, mode, SCE_GXM_TRANSFER_LINEAR>(mem, src, dst, dst_type, key_value, key_mask);
        break;
    case SCE_GXM_TRANSFER_SWIZZLED:
        perform_transfer_copy_dst_type<T, mode, SCE_GXM_TRANSFER_SWIZZLED>(mem, src, dst, dst_type, key_value, key_mask);
        break;
    case SCE_GXM_TRANSFER_TILED:
        perform_transfer_copy_dst_type<T, mode, SCE_GXM_TRANSFER_TILED>(mem, src, dst, dst_type, key_value, key_mask);
        break;
    default:
        LOG_ERROR("Unknown transfer key mode {}", fmt::underlying(mode));
        break;
    }
}

template <typename T>
static void perform_transfer_copy_mode(MemState &mem, const SceGxmTransferImage &src, const SceGxmTransferImage &dst, SceGxmTransferType src_type, SceGxmTransferType dst_type, uint32_t key_value, uint32_t key_mask, SceGxmTransferColorKeyMode mode) {
    switch (mode) {
    case SCE_GXM_TRANSFER_COLORKEY_NONE:
        perform_transfer_copy_src_type<T, SCE_GXM_TRANSFER_COLORKEY_NONE>(mem, src, dst, src_type, dst_type, key_value, key_mask);
        break;
    case SCE_GXM_TRANSFER_COLORKEY_PASS:
        perform_transfer_copy_src_type<T, SCE_GXM_TRANSFER_COLORKEY_PASS>(mem, src, dst, src_type, dst_type, key_value, key_mask);
        break;
    case SCE_GXM_TRANSFER_COLORKEY_REJECT:
        perform_transfer_copy_src_type<T, SCE_GXM_TRANSFER_COLORKEY_REJECT>(mem, src, dst, src_type, dst_type, key_value, key_mask);
        break;
    default:
        LOG_ERROR("Unknown transfer key mode {}", fmt::underlying(mode));
        break;
    }
}

COMMAND(handle_transfer_copy) {
    TRACY_FUNC_COMMANDS(handle_transfer_copy);
    const uint32_t colorKeyValue = helper.pop<uint32_t>();
    const uint32_t colorKeyMask = helper.pop<uint32_t>();
    SceGxmTransferColorKeyMode colorKeyMode = helper.pop<SceGxmTransferColorKeyMode>();
    const SceGxmTransferImage *images = helper.pop<SceGxmTransferImage *>();
    const SceGxmTransferFormat src_fmt = images[0].format;
    const SceGxmTransferFormat dst_fmt = images[1].format;
    SceGxmTransferType src_type = helper.pop<SceGxmTransferType>();
    SceGxmTransferType dst_type = helper.pop<SceGxmTransferType>();

    if (use_gpu_depth_transfer && renderer.current_backend == Backend::Vulkan) {
        auto &vk_state = dynamic_cast<vulkan::VKState &>(renderer);
        if (vk_state.surface_cache.try_transfer_depth_gpu(images[0].address.address(), images[1].address.address(), images[0].width, images[0].height)) {
            delete[] images;
            return;
        }
    }

    if (src_fmt != dst_fmt) {
        LOG_ERROR_ONCE("Unhandled format conversion from 0x{:0X} to 0x{:0X}", fmt::underlying(src_fmt), fmt::underlying(dst_fmt));
        delete[] images;
        return;
    }

    if (colorKeyMode != SCE_GXM_TRANSFER_COLORKEY_NONE && src_fmt != SCE_GXM_TRANSFER_FORMAT_U8U8U8U8_ABGR) {
        LOG_ERROR_ONCE("Transfer copy with non-zero key mask not handled for format 0x{:0X}", fmt::underlying(src_fmt));
    }

    vulkan::CallbackRequestFunction copy_operation = [=, &mem]() {
        const SceGxmTransferImage &src = images[0];
        const SceGxmTransferImage &dst = images[1];

        // Get bits per pixel of the image
        const uint32_t bpp = gxm::get_bits_per_pixel(src.format);

        // use a specialized function for each type (more optimized)
        switch (bpp) {
        case 8:
            perform_transfer_copy_src_type<uint8_t, SCE_GXM_TRANSFER_COLORKEY_NONE>(mem, src, dst, src_type, dst_type, colorKeyValue, colorKeyMask);
            break;
        case 16:
            perform_transfer_copy_src_type<uint16_t, SCE_GXM_TRANSFER_COLORKEY_NONE>(mem, src, dst, src_type, dst_type, colorKeyValue, colorKeyMask);
            break;
        case 24:
            perform_transfer_copy_src_type<std::array<uint8_t, 3>, SCE_GXM_TRANSFER_COLORKEY_NONE>(mem, src, dst, src_type, dst_type, colorKeyValue, colorKeyMask);
            break;
        case 32:
            perform_transfer_copy_mode<uint32_t>(mem, src, dst, src_type, dst_type, colorKeyValue, colorKeyMask, colorKeyMode);
            break;
        case 64:
            perform_transfer_copy_src_type<uint64_t, SCE_GXM_TRANSFER_COLORKEY_NONE>(mem, src, dst, src_type, dst_type, colorKeyValue, colorKeyMask);
            break;
        case 128:
            perform_transfer_copy_src_type<std::array<uint64_t, 2>, SCE_GXM_TRANSFER_COLORKEY_NONE>(mem, src, dst, src_type, dst_type, colorKeyValue, colorKeyMask);
            break;
        }

        delete[] images;
    };

    if (renderer.current_backend == Backend::Vulkan && renderer.features.enable_memory_mapping && !renderer.disable_surface_sync) {
        const uint32_t src_read_size = images[0].stride * (images[0].y + images[0].height);
        const uint32_t dst_write_size = images[1].stride * (images[1].y + images[1].height);
        if (dynamic_cast<vulkan::VKState &>(renderer).surface_cache.check_for_surface(mem, images[0].address.address(), copy_operation, images[1].address.address() /*, src_read_size, dst_write_size*/)) {
            LOG_WARN_ONCE("transfer_copy: surface-synced src=0x{:08X}→dst=0x{:08X} fmt=0x{:X} {}x{} srcXY=({},{}) dstXY=({},{})",
                images[0].address.address(), images[1].address.address(),
                fmt::underlying(src_fmt), images[0].width, images[0].height,
                images[0].x, images[0].y, images[1].x, images[1].y);
            return;
        }

        LOG_WARN_ONCE("transfer_copy: no surface sync for src=0x{:08X}→dst=0x{:08X} fmt=0x{:X} {}x{}",
            images[0].address.address(), images[1].address.address(),
            fmt::underlying(src_fmt), images[0].width, images[0].height);
    }

    copy_operation();
}

static uint64_t transfer_region_bytes(const SceGxmTransferImage &img, uint32_t pixel_bytes) {
    if (img.height == 0 || img.width == 0 || img.stride <= 0)
        return 0;
    return static_cast<uint64_t>(img.stride) * (img.height - 1) + static_cast<uint64_t>(img.width) * pixel_bytes;
}

COMMAND(handle_transfer_downscale) {
    TRACY_FUNC_COMMANDS(handle_transfer_downscale);
    SceGxmTransferImage *src = helper.pop<SceGxmTransferImage *>();
    SceGxmTransferImage *dst = helper.pop<SceGxmTransferImage *>();

    if (src->format != dst->format) {
        LOG_ERROR_ONCE("Unhandled format conversion from 0x{:0X} to 0x{:0X}", fmt::underlying(src->format), fmt::underlying(dst->format));
        return;
    }

    // adjust the x/y value
    const uint32_t pixel_bytes = gxm::get_bits_per_pixel(src->format) / 8;
    const Address src_surface_base = src->address.address();
    src->address = (src->address.cast<uint8_t>() + src->y * src->stride + src->x * pixel_bytes).cast<void>();
    dst->address = (dst->address.cast<uint8_t>() + dst->y * dst->stride + dst->x * pixel_bytes).cast<void>();

    // only rgb formats are supported by the PS Vita for downscaling
    vulkan::CallbackRequestFunction downscale_operation = [&mem, src, dst, pixel_bytes]() {
        AVPixelFormat pixel_fmt = AV_PIX_FMT_NONE;
        switch (src->format) {
        case SCE_GXM_TRANSFER_FORMAT_U5U6U5_BGR:
            pixel_fmt = AV_PIX_FMT_RGB565LE;
            break;
        case SCE_GXM_TRANSFER_FORMAT_U8U8U8_BGR:
            pixel_fmt = AV_PIX_FMT_RGB24;
            break;
        case SCE_GXM_TRANSFER_FORMAT_U8U8U8U8_ABGR:
            pixel_fmt = AV_PIX_FMT_RGBA;
            break;
        default:
            break;
        }

        // In Page Table mode a range can span and libswscale can read past the last row of its input
        const uint64_t src_bytes = transfer_region_bytes(*src, pixel_bytes);
        const uint64_t dst_bytes = transfer_region_bytes(*dst, pixel_bytes);
        const bool can_stage = src_bytes > 0 && dst_bytes > 0;
        const Address src_addr = src->address.address();
        const Address dst_addr = dst->address.address();

        if (can_stage
            && (!is_valid_addr_range(mem, src_addr, static_cast<Address>(src_addr + src_bytes))
                || !is_valid_addr_range(mem, dst_addr, static_cast<Address>(dst_addr + dst_bytes)))) {
            LOG_WARN_ONCE("[XFER] downscale skipped: src 0x{:08X}+{} or dst 0x{:08X}+{} is not fully allocated guest memory",
                src_addr, src_bytes, dst_addr, dst_bytes);
            delete src;
            delete dst;
            return;
        }

        constexpr uint32_t sws_input_padding = 64;
        std::vector<uint8_t> src_staging, dst_staging;
        uint8_t *src_ptr = nullptr;
        uint8_t *dst_ptr = nullptr;
        if (can_stage) {
            src_staging.assign(static_cast<size_t>(src_bytes) + sws_input_padding, 0);
            memcpy_from_guest(mem, src_staging.data(), src_addr, static_cast<uint32_t>(src_bytes));
            // read-modify-write: whatever the scaler does not touch (inter-row padding) must survive
            dst_staging.resize(static_cast<size_t>(dst_bytes));
            memcpy_from_guest(mem, dst_staging.data(), dst_addr, static_cast<uint32_t>(dst_bytes));
            src_ptr = src_staging.data();
            dst_ptr = dst_staging.data();
        } else {
            src_ptr = src->address.cast<uint8_t>().get(mem);
            dst_ptr = dst->address.cast<uint8_t>().get(mem);
        }

        if (pixel_fmt != AV_PIX_FMT_NONE && src->stride > 0 && dst->stride > 0) {
            // use ffmpeg with the avg filter
            SwsContext *ctx = sws_getContext(src->width, src->height, pixel_fmt, dst->width, dst->height, pixel_fmt, SWS_AREA, nullptr, nullptr, nullptr);
            if (ctx == nullptr) {
                LOG_ERROR("Failed to get ffmpeg context for format 0x{:0X}", fmt::underlying(src->format));
            } else {
                const uint8_t *const src_planes[4] = { src_ptr, nullptr, nullptr, nullptr };
                uint8_t *const dst_planes[4] = { dst_ptr, nullptr, nullptr, nullptr };
                const int src_strides[4] = { src->stride, 0, 0, 0 };
                const int dst_strides[4] = { dst->stride, 0, 0, 0 };
                sws_scale(ctx, src_planes, src_strides, 0, src->height, dst_planes, dst_strides);
                sws_freeContext(ctx);
            }

        } else {
            // fallback, not (entirely) supported by ffmpeg
            // slow and not entirely accurate (nearest instead of average) fallback

            auto perform_downscale = [&]<typename T>(T type) {
                for (size_t y = 0; y < dst->height; y++) {
                    // stride is in bytes
                    T *src_line = reinterpret_cast<T *>(src_ptr + (size_t)src->stride * y * 2);
                    T *dst_line = reinterpret_cast<T *>(dst_ptr + (size_t)dst->stride * y);
                    for (size_t x = 0; x < dst->width; x++) {
                        dst_line[x] = src_line[2 * x];
                    }
                }
            };
            switch (gxm::get_bits_per_pixel(src->format)) {
            case 8:
                perform_downscale(uint8_t());
                break;
            case 16:
                perform_downscale(uint16_t());
                break;
            case 24:
                perform_downscale(std::array<uint8_t, 3>());
                break;
            case 32:
                perform_downscale(uint32_t());
                break;
            default:
                // should not happen
                LOG_ERROR("Unhandled format 0x{:0X}", fmt::underlying(src->format));
                break;
            }
        }

        if (can_stage)
            memcpy_to_guest(mem, dst_addr, dst_staging.data(), static_cast<uint32_t>(dst_bytes));

        delete src;
        delete dst;
    };

    const auto log_src = *src;
    const auto log_dst = *dst;

    bool gpu_path = false;
    if (renderer.current_backend == Backend::Vulkan && renderer.features.enable_memory_mapping && !renderer.disable_surface_sync) {
        // src->address has already been adjusted to the first read byte above
        gpu_path = dynamic_cast<vulkan::VKState &>(renderer).surface_cache.check_for_surface(mem, src->address.address(), downscale_operation, dst->address.address(), static_cast<uint32_t>(std::min<uint64_t>(transfer_region_bytes(*src, pixel_bytes), UINT32_MAX)));
    }

    static std::atomic<uint32_t> downscales{ 0 };
    const uint32_t n = downscales.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 4 || (n & 1023) == 0)
        LOG_INFO("[XFER] downscale #{}: fmt 0x{:X} src 0x{:08X} (surface base 0x{:08X}) xy=({},{}) {}x{} stride {} -> dst 0x{:08X} xy=({},{}) {}x{} stride {} | {}",
            n, fmt::underlying(log_src.format), log_src.address.address(), src_surface_base, log_src.x, log_src.y, log_src.width, log_src.height, log_src.stride,
            log_dst.address.address(), log_dst.x, log_dst.y, log_dst.width, log_dst.height, log_dst.stride, gpu_path ? "GPU surface path" : "CPU path (staged)");

    if (gpu_path)
        // let the vulkan surface cache handle it
        return;

    downscale_operation();
}

COMMAND(handle_transfer_fill) {
    TRACY_FUNC_COMMANDS(handle_transfer_fill);
    const uint32_t fill_color = helper.pop<uint32_t>();
    const SceGxmTransferImage *dest = helper.pop<SceGxmTransferImage *>();

    const auto bpp = gxm::get_bits_per_pixel(dest->format);

    const uint32_t bytes_per_pixel = (bpp + 7) >> 3;
    for (uint32_t y = 0; y < dest->height; y++) {
        for (uint32_t x = 0; x < dest->width; x++) {
            // Set offset of destination
            const auto dest_offset = ((x + dest->x) * bytes_per_pixel) + ((y + dest->y) * dest->stride);

            // Set pointer of destination
            auto dest_ptr = (uint8_t *)dest->address.get(mem) + dest_offset;

            // Fill color in destination
            memcpy(dest_ptr, &fill_color, bytes_per_pixel);
        }
    }

    // TODO: handle case where dest is a cached surface

    delete dest;
}

} // namespace renderer
