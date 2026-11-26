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
#include <cstring>
#include <limits>
#include <vector>

#include <gxm/functions.h>
#include <gxm/types.h>
#include <mem/functions.h>
#include <renderer/commands.h>
#include <renderer/driver_functions.h>
#include <renderer/functions.h>
#include <renderer/state.h>
#include <renderer/transfer_fill.h>
#include <renderer/transfer_model.h>
#include <renderer/types.h>
#include <util/log.h>
#include <util/tracy.h>
#if ECV_ORACLE
#include <ecv_gpu_trace.h>
#endif

#include <renderer/vulkan/state.h>
#include <renderer/vulkan/surface_cache.h>

// keywords.h must be after tracy.h for msvc compiler
#include <util/keywords.h>

namespace renderer {

// Perform a depth-to-depth sceGxmTransferCopy on the GPU (false = old behaviour i.e. transfer does nothing)
static constexpr bool use_gpu_depth_transfer = true;

template <typename T, SceGxmTransferColorKeyMode mode, SceGxmTransferType src_type, SceGxmTransferType dst_type>
static void perform_transfer_copy_impl(MemState &mem, const SceGxmTransferImage &src, const SceGxmTransferImage &dst, uint32_t key_value, uint32_t key_mask) {
    const uint8_t *src_ptr = src.address.cast<uint8_t>().get(mem);
    uint8_t *dst_ptr = dst.address.cast<uint8_t>().get(mem);

    if (src.width == 0 || src.height == 0)
        return;

    auto compute_offset = [&](uint32_t dx, uint32_t dy, const SceGxmTransferImage &img, SceGxmTransferType type) -> int64_t {
        constexpr int64_t pixel_bytes = sizeof(T);
        if (type == SCE_GXM_TRANSFER_LINEAR) {
            return dy * static_cast<int64_t>(img.stride) + dx * pixel_bytes;
        } else if (type == SCE_GXM_TRANSFER_TILED) {
            // tiles are 32x32, you have the offset within the tile then the offset of the tile
            const int64_t stride_pixel = img.stride / pixel_bytes;
            const uint32_t texel_offset_in_tile = ((dy % 32) * 32) + (dx % 32);
            const int64_t tile_address = (stride_pixel / 32) * (dy / 32) + (dx / 32);

            return (tile_address * 1024 + texel_offset_in_tile) * pixel_bytes;
        } else {
            return texture::encode_morton(dx, dy, img.width, img.height) * pixel_bytes;
        }
    };

    if constexpr (src_type == SCE_GXM_TRANSFER_LINEAR && dst_type == SCE_GXM_TRANSFER_LINEAR && mode == SCE_GXM_TRANSFER_COLORKEY_NONE) {
        const size_t row_bytes = static_cast<size_t>(src.width) * sizeof(T);
        if (src.stride == static_cast<int64_t>(row_bytes) && dst.stride == static_cast<int64_t>(row_bytes)) {
            memmove(dst_ptr + compute_offset(dst.x, dst.y, dst, dst_type),
                src_ptr + compute_offset(src.x, src.y, src, src_type), row_bytes * src.height);
            return;
        }
    }

    auto transfer_range = [&](const uint8_t *base, const SceGxmTransferImage &image, SceGxmTransferType type) {
        int64_t first = std::numeric_limits<int64_t>::max();
        int64_t last = std::numeric_limits<int64_t>::min();
        auto include = [&](uint32_t x, uint32_t y) {
            const int64_t offset = compute_offset(x, y, image, type);
            first = std::min(first, offset);
            last = std::max(last, offset);
        };
        if (type == SCE_GXM_TRANSFER_LINEAR) {
            include(image.x, image.y);
            include(image.x + src.width - 1, image.y);
            include(image.x, image.y + src.height - 1);
            include(image.x + src.width - 1, image.y + src.height - 1);
        } else {
            for (uint32_t dy = 0; dy < src.height; ++dy) {
                if (type == SCE_GXM_TRANSFER_SWIZZLED) {
                    for (uint32_t dx = 0; dx < src.width; ++dx)
                        include(image.x + dx, image.y + dy);
                } else {
                    include(image.x, image.y + dy);
                    include(image.x + src.width - 1, image.y + dy);
                }
            }
        }
        return std::pair{ reinterpret_cast<uintptr_t>(base + first), reinterpret_cast<uintptr_t>(base + last + sizeof(T)) };
    };
    const auto [src_begin, src_end] = transfer_range(src_ptr, src, src_type);
    const auto [dst_begin, dst_end] = transfer_range(dst_ptr, dst, dst_type);
    const bool overlaps = src_begin < dst_end && dst_begin < src_end;

    // Snapshot only the transferred pixels; copying whole strides can read padding or unrelated tiles.
    std::vector<T> src_copy;
    auto read_source = [&](int64_t offset) {
        T value;
        memcpy(&value, src_ptr + offset, sizeof(T));
        return value;
    };
    if (overlaps) {
        src_copy.resize(static_cast<size_t>(src.width) * src.height);
        for (uint32_t dy = 0; dy < src.height; ++dy) {
            for (uint32_t dx = 0; dx < src.width; ++dx)
                src_copy[static_cast<size_t>(dy) * src.width + dx] = read_source(compute_offset(src.x + dx, src.y + dy, src, src_type));
        }
    } else if constexpr (src_type == SCE_GXM_TRANSFER_LINEAR && dst_type == SCE_GXM_TRANSFER_LINEAR && mode == SCE_GXM_TRANSFER_COLORKEY_NONE) {
        for (uint32_t dy = 0; dy < src.height; ++dy) {
            memcpy(dst_ptr + compute_offset(dst.x, dst.y + dy, dst, dst_type),
                src_ptr + compute_offset(src.x, src.y + dy, src, src_type), static_cast<size_t>(src.width) * sizeof(T));
        }
        return;
    }

    for (uint32_t dy = 0; dy < src.height; dy++) {
        for (uint32_t dx = 0; dx < src.width; dx++) {
            // compute offset depending on the texture type used
            // the function compute_offset gets inlined
            const int64_t src_offset = compute_offset(src.x + dx, src.y + dy, src, src_type);
            const int64_t dst_offset = compute_offset(dst.x + dx, dst.y + dy, dst, dst_type);

            T value = overlaps ? src_copy[static_cast<size_t>(dy) * src.width + dx] : read_source(src_offset);
            if constexpr (mode == SCE_GXM_TRANSFER_COLORKEY_PASS) {
                if ((value & key_mask) != key_value)
                    continue;
            } else if constexpr (mode == SCE_GXM_TRANSFER_COLORKEY_REJECT) {
                if ((value & key_mask) == key_value)
                    continue;
            }

            memcpy(dst_ptr + dst_offset, &value, sizeof(T));
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

// Byte offset of a pixel of a transfer image (any layout), the runtime twin of compute_offset above
static int64_t transfer_pixel_offset(const SceGxmTransferImage &img, SceGxmTransferType type, uint32_t x, uint32_t y, uint32_t pixel_bytes) {
    if (type == SCE_GXM_TRANSFER_LINEAR)
        return y * static_cast<int64_t>(img.stride) + static_cast<int64_t>(x) * pixel_bytes;
    if (type == SCE_GXM_TRANSFER_TILED) {
        const int64_t stride_pixel = img.stride / pixel_bytes;
        const uint32_t texel_offset_in_tile = ((y % 32) * 32) + (x % 32);
        const int64_t tile_address = (stride_pixel / 32) * (y / 32) + (x / 32);
        return (tile_address * 1024 + texel_offset_in_tile) * pixel_bytes;
    }
    return static_cast<int64_t>(texture::encode_morton(x, y, img.width, img.height)) * pixel_bytes;
}

// Copy between two different colour formats: every pixel is converted exactly (transfer::convert_pixel). The source is read
// completely before anything is written, so overlapping images are fine.
static void perform_transfer_convert(MemState &mem, const SceGxmTransferImage &src, const SceGxmTransferImage &dst, SceGxmTransferType src_type, SceGxmTransferType dst_type,
    uint32_t key_value, uint32_t key_mask, SceGxmTransferColorKeyMode mode) {
    if (src.width == 0 || src.height == 0)
        return;
    const transfer::FormatDesc src_desc = transfer::describe(src.format);
    const transfer::FormatDesc dst_desc = transfer::describe(dst.format);
    const uint8_t *src_ptr = src.address.cast<uint8_t>().get(mem);
    uint8_t *dst_ptr = dst.address.cast<uint8_t>().get(mem);

    std::vector<uint8_t> converted(static_cast<size_t>(src.width) * src.height * dst_desc.bytes);
    std::vector<uint8_t> keep(static_cast<size_t>(src.width) * src.height, 1);
    for (uint32_t y = 0; y < src.height; y++) {
        for (uint32_t x = 0; x < src.width; x++) {
            const uint8_t *in = src_ptr + transfer_pixel_offset(src, src_type, src.x + x, src.y + y, src_desc.bytes);
            const size_t index = static_cast<size_t>(y) * src.width + x;
            if (mode != SCE_GXM_TRANSFER_COLORKEY_NONE) {
                const bool match = (transfer::load_pixel(src_desc, in) & key_mask) == key_value;
                keep[index] = (mode == SCE_GXM_TRANSFER_COLORKEY_PASS) == match ? 1 : 0;
            }
            transfer::convert_pixel(src_desc, dst_desc, in, &converted[index * dst_desc.bytes]);
        }
    }
    for (uint32_t y = 0; y < src.height; y++) {
        for (uint32_t x = 0; x < src.width; x++) {
            const size_t index = static_cast<size_t>(y) * src.width + x;
            if (keep[index])
                memcpy(dst_ptr + transfer_pixel_offset(dst, dst_type, dst.x + x, dst.y + y, dst_desc.bytes), &converted[index * dst_desc.bytes], dst_desc.bytes);
        }
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
#if ECV_ORACLE
    if (ecv::oracle::gpu::enabled())
        ecv::oracle::gpu::xfer("copy", images[0].address.address(), static_cast<uint32_t>(src_fmt), images[0].x, images[0].y, images[0].width, images[0].height, images[0].stride,
            images[1].address.address(), static_cast<uint32_t>(dst_fmt), images[1].x, images[1].y, images[1].width, images[1].height, images[1].stride,
            static_cast<uint32_t>(colorKeyMode));
#endif
    SceGxmTransferType dst_type = helper.pop<SceGxmTransferType>();

    if (use_gpu_depth_transfer && renderer.current_backend == Backend::Vulkan) {
        auto &vk_state = dynamic_cast<vulkan::VKState &>(renderer);
        if (vk_state.surface_cache.try_transfer_depth_gpu(images[0].address.address(), images[1].address.address(), images[0].width, images[0].height)) {
            delete[] images;
            return;
        }
    }

    if (colorKeyMode == SCE_GXM_TRANSFER_COLORKEY_NONE && renderer.current_backend == Backend::Vulkan
        && dynamic_cast<vulkan::VKState &>(renderer).surface_cache.try_transfer_color_gpu(mem, vulkan::VKSurfaceCache::ColorTransferKind::Copy, images[0], images[1], src_type, dst_type, 0)) {
        delete[] images;
        return;
    }

    if (src_fmt != dst_fmt && !transfer::convertible(src_fmt, dst_fmt)) {
        LOG_ERROR_ONCE("Unhandled format conversion from 0x{:0X} to 0x{:0X}", fmt::underlying(src_fmt), fmt::underlying(dst_fmt));
        delete[] images;
        return;
    }

    if (colorKeyMode != SCE_GXM_TRANSFER_COLORKEY_NONE && (src_fmt != dst_fmt || transfer::pixel_bytes(src_fmt) > 8 || transfer::pixel_bytes(src_fmt) == 3)) {
        LOG_ERROR_ONCE("Transfer copy with non-zero key mask not handled for format 0x{:0X}", fmt::underlying(src_fmt));
    }

    vulkan::CallbackRequestFunction copy_operation = [=, &mem]() {
        const SceGxmTransferImage &src = images[0];
        const SceGxmTransferImage &dst = images[1];

        if (src.format != dst.format) {
            perform_transfer_convert(mem, src, dst, src_type, dst_type, colorKeyValue, colorKeyMask, colorKeyMode);
            delete[] images;
            return;
        }

        // Get bits per pixel of the image
        const uint32_t bpp = gxm::get_bits_per_pixel(src.format);

        // use a specialized function for each type (more optimized)
        switch (bpp) {
        case 8:
            perform_transfer_copy_mode<uint8_t>(mem, src, dst, src_type, dst_type, colorKeyValue, colorKeyMask, colorKeyMode);
            break;
        case 16:
            perform_transfer_copy_mode<uint16_t>(mem, src, dst, src_type, dst_type, colorKeyValue, colorKeyMask, colorKeyMode);
            break;
        case 24:
            perform_transfer_copy_src_type<std::array<uint8_t, 3>, SCE_GXM_TRANSFER_COLORKEY_NONE>(mem, src, dst, src_type, dst_type, colorKeyValue, colorKeyMask);
            break;
        case 32:
            perform_transfer_copy_mode<uint32_t>(mem, src, dst, src_type, dst_type, colorKeyValue, colorKeyMask, colorKeyMode);
            break;
        case 64:
            perform_transfer_copy_mode<uint64_t>(mem, src, dst, src_type, dst_type, colorKeyValue, colorKeyMask, colorKeyMode);
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

// The exact 2x2 box downscale of linear images (no libswscale: no scaler context per call, no padding copies, a rounding
// that is the same on every host). Formats the box filter has no meaning for (YUV, RAW) take the top left texel of each 2x2.
static void perform_transfer_downscale(MemState &mem, const SceGxmTransferImage &src, const SceGxmTransferImage &dst) {
    const uint32_t src_bytes_px = transfer::pixel_bytes(src.format);
    const uint32_t dst_bytes_px = transfer::pixel_bytes(dst.format);
    if (src_bytes_px == 0 || dst_bytes_px == 0 || src.width == 0 || src.height == 0 || dst.width == 0 || dst.height == 0 || src.stride <= 0 || dst.stride <= 0)
        return;
    const uint64_t src_bytes = transfer_region_bytes(src, src_bytes_px);
    const uint64_t dst_bytes = transfer_region_bytes(dst, dst_bytes_px);
    const Address src_addr = src.address.address();
    const Address dst_addr = dst.address.address();
    if (src_bytes == 0 || dst_bytes == 0 || !is_valid_addr_range(mem, src_addr, static_cast<Address>(src_addr + src_bytes))
        || !is_valid_addr_range(mem, dst_addr, static_cast<Address>(dst_addr + dst_bytes))) {
        LOG_WARN_ONCE("Downscale skipped: src 0x{:08X}+{} or dst 0x{:08X}+{} is not fully allocated guest memory", src_addr, src_bytes, dst_addr, dst_bytes);
        return;
    }
    // Stage through the guest-memory helpers (protected/trapped pages, page-table mapping) instead of raw pointers.
    std::vector<uint8_t> src_staging(static_cast<size_t>(src_bytes));
    std::vector<uint8_t> dst_staging(static_cast<size_t>(dst_bytes));
    memcpy_from_guest(mem, src_staging.data(), src_addr, static_cast<uint32_t>(src_bytes));
    memcpy_from_guest(mem, dst_staging.data(), dst_addr, static_cast<uint32_t>(dst_bytes));
    const uint8_t *src_ptr = src_staging.data();
    uint8_t *dst_ptr = dst_staging.data();
    const transfer::FormatDesc src_desc = transfer::describe(src.format);
    const transfer::FormatDesc dst_desc = transfer::describe(dst.format);
    const bool box = src_desc.bytes != 0 && (src.format == dst.format || dst_desc.bytes != 0);
    uint8_t averaged[4];

    for (uint32_t y = 0; y < dst.height; y++) {
        // the last row/column of a source that is not exactly twice as big repeats
        const uint32_t sy0 = std::min(2 * y, src.height - 1), sy1 = std::min(2 * y + 1, src.height - 1);
        for (uint32_t x = 0; x < dst.width; x++) {
            const uint32_t sx0 = std::min(2 * x, src.width - 1), sx1 = std::min(2 * x + 1, src.width - 1);
            const auto at = [&](uint32_t px, uint32_t py) { return src_ptr + static_cast<size_t>(py) * src.stride + static_cast<size_t>(px) * src_bytes_px; };
            uint8_t *out = dst_ptr + static_cast<size_t>(y) * dst.stride + static_cast<size_t>(x) * dst_bytes_px;
            if (!box) {
                memcpy(out, at(sx0, sy0), std::min(src_bytes_px, dst_bytes_px));
            } else if (src.format == dst.format) {
                transfer::average_2x2(src_desc, at(sx0, sy0), at(sx1, sy0), at(sx0, sy1), at(sx1, sy1), out);
            } else {
                transfer::average_2x2(src_desc, at(sx0, sy0), at(sx1, sy0), at(sx0, sy1), at(sx1, sy1), averaged);
                transfer::convert_pixel(src_desc, dst_desc, averaged, out);
            }
        }
    }
    memcpy_to_guest(mem, dst_addr, dst_staging.data(), static_cast<uint32_t>(dst_bytes));
}

COMMAND(handle_transfer_downscale) {
    TRACY_FUNC_COMMANDS(handle_transfer_downscale);
    SceGxmTransferImage *src = helper.pop<SceGxmTransferImage *>();
    SceGxmTransferImage *dst = helper.pop<SceGxmTransferImage *>();
#if ECV_ORACLE
    if (ecv::oracle::gpu::enabled())
        ecv::oracle::gpu::xfer("down", src->address.address(), static_cast<uint32_t>(src->format), src->x, src->y, src->width, src->height, src->stride,
            dst->address.address(), static_cast<uint32_t>(dst->format), dst->x, dst->y, dst->width, dst->height, dst->stride, 0);
#endif

    if (renderer.current_backend == Backend::Vulkan
        && dynamic_cast<vulkan::VKState &>(renderer).surface_cache.try_transfer_color_gpu(mem, vulkan::VKSurfaceCache::ColorTransferKind::Downscale, *src, *dst,
            SCE_GXM_TRANSFER_LINEAR, SCE_GXM_TRANSFER_LINEAR, 0)) {
        delete src;
        delete dst;
        return;
    }

    if (src->format != dst->format && !transfer::convertible(src->format, dst->format)) {
        LOG_ERROR_ONCE("Unhandled format conversion from 0x{:0X} to 0x{:0X}", fmt::underlying(src->format), fmt::underlying(dst->format));
        delete src;
        delete dst;
        return;
    }

    // adjust the x/y value
    const uint32_t pixel_bytes = transfer::pixel_bytes(src->format);
    src->address = (src->address.cast<uint8_t>() + src->y * src->stride + src->x * pixel_bytes).cast<void>();
    dst->address = (dst->address.cast<uint8_t>() + dst->y * dst->stride + dst->x * transfer::pixel_bytes(dst->format)).cast<void>();

    vulkan::CallbackRequestFunction downscale_operation = [&mem, src, dst]() {
        perform_transfer_downscale(mem, *src, *dst);
        delete src;
        delete dst;
    };

    bool surface_synced = false;
    if (renderer.current_backend == Backend::Vulkan && renderer.features.enable_memory_mapping && !renderer.disable_surface_sync) {
        // src->address has already been adjusted to the first read byte above
        surface_synced = dynamic_cast<vulkan::VKState &>(renderer).surface_cache.check_for_surface(mem, src->address.address(), downscale_operation, dst->address.address(), static_cast<uint32_t>(std::min<uint64_t>(transfer_region_bytes(*src, pixel_bytes), UINT32_MAX)));
    }

    if (surface_synced)
        // the surface cache runs it once the GPU surface has been written back to guest memory
        return;

    downscale_operation();
}

COMMAND(handle_transfer_fill) {
    TRACY_FUNC_COMMANDS(handle_transfer_fill);
    const uint32_t fill_color = helper.pop<uint32_t>();
    const SceGxmTransferImage *dest = helper.pop<SceGxmTransferImage *>();

#if ECV_ORACLE
    if (ecv::oracle::gpu::enabled())
        ecv::oracle::gpu::xfer("fill", 0, 0, 0, 0, 0, 0, 0, dest->address.address(), static_cast<uint32_t>(dest->format), dest->x, dest->y, dest->width, dest->height,
            dest->stride, fill_color);
#endif
    if (renderer.current_backend == Backend::Vulkan
        && dynamic_cast<vulkan::VKState &>(renderer).surface_cache.try_transfer_color_gpu(mem, vulkan::VKSurfaceCache::ColorTransferKind::Fill, *dest, *dest,
            SCE_GXM_TRANSFER_LINEAR, SCE_GXM_TRANSFER_LINEAR, fill_color)) {
        delete dest;
        return;
    }
    const auto bpp = gxm::get_bits_per_pixel(dest->format);

    const uint32_t bytes_per_pixel = transfer_fill_pixel_bytes(bpp);
    uint8_t pixel[transfer_fill_max_pixel_bytes];
    uint64_t first_byte = 0, end_byte = 0;
    if (build_transfer_fill_pixel(fill_color, bytes_per_pixel, pixel) == 0) {
        LOG_ERROR("Transfer fill: unsupported destination format 0x{:0X}", fmt::underlying(dest->format));
    } else if (dest->width != 0 && dest->height != 0) {
        if (!transfer_fill_span(dest->address.address(), dest->x, dest->y, dest->width, dest->height, dest->stride, bytes_per_pixel, first_byte, end_byte)
            || end_byte > UINT32_MAX
            || !is_valid_addr_range(mem, static_cast<Address>(first_byte), static_cast<Address>(end_byte))) {
            // guest controlled geometry: never write outside mapped guest memory
            LOG_ERROR("Transfer fill: destination {} ({}x{} at {},{} stride {}) is outside guest memory",
                log_hex(dest->address.address()), dest->width, dest->height, dest->x, dest->y, dest->stride);
        } else {
            // build the row once, then fill row by row
            std::vector<uint8_t> row(static_cast<size_t>(dest->width) * bytes_per_pixel);
            fill_transfer_row(row.data(), dest->width, pixel, bytes_per_pixel);

            uint8_t *base = dest->address.cast<uint8_t>().get(mem);
            for (uint32_t y = 0; y < dest->height; y++) {
                const int64_t offset = (static_cast<int64_t>(y) + dest->y) * dest->stride + static_cast<int64_t>(dest->x) * bytes_per_pixel;
                memcpy(base + offset, row.data(), row.size());
            }

            // The fill bypassed the GPU: a cached surface covering these bytes must re-upload them.
            // (The GL surface cache has no CPU-write tracking to hook into.)
            if (renderer.current_backend == Backend::Vulkan)
                dynamic_cast<vulkan::VKState &>(renderer).surface_cache.mark_guest_range_written(static_cast<Address>(first_byte), static_cast<Address>(end_byte));
        }
    }

    delete dest;
}

} // namespace renderer
