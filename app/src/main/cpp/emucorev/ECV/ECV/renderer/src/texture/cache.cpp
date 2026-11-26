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

#include <renderer/functions.h>

#include <chrono>
#include <cpu/functions.h>
#include <renderer/profile.h>
#include <renderer/texture_cache.h>
#include <renderer/yuv.h>

#include <gxm/functions.h>
#include <mem/functions.h>
#include <mem/state.h>
#include <mem/ptr.h>
#include <util/align.h>
#include <util/log.h>

#include <algorithm>
#include <cstring>
#include <numeric>
#if defined(__x86_64__) && !defined(__APPLE__)
#include <xxh_x86dispatch.h>
#else
#define XXH_INLINE_ALL
#include <xxhash.h>
#if ECV_ORACLE
#include <ecv_gpu_trace.h>
#endif
#endif

namespace renderer {
namespace texture {

static uint64_t hash_data(const void *data, size_t size) {
    return XXH3_64bits(data, size);
}

static size_t palette_entry_count(SceGxmTextureBaseFormat base_format) {
    return base_format == SCE_GXM_TEXTURE_BASE_FORMAT_P4 ? 16 : 256;
}

uint64_t hash_texture_palette(const SceGxmTexture &texture, const MemState &mem) {
    const SceGxmTextureBaseFormat base_format = gxm::get_base_format(gxm::get_format(texture));
    const size_t bytes = palette_entry_count(base_format) * sizeof(uint32_t);
    const Address address = static_cast<Address>(texture.palette_addr) << 6;
    if (!address || !is_valid_addr_range(mem, address, address + static_cast<Address>(bytes)))
        return 0;
    return hash_data(get_texture_palette(texture, mem), bytes);
}

uint64_t hash_texture_pixels(const SceGxmTexture &texture, uint32_t texture_size, const MemState &mem) {
    const Ptr<const void> data(texture.data_addr << 2);

    // the texture memory may have been freed by the game while this bind was queued
    if (data.address() && !is_valid_addr_range(mem, data.address(), data.address() + texture_size)) {
        LOG_WARN_ONCE("Texture data not in valid memory (addr=0x{:08X} size={}), not hashing", data.address(), texture_size);
        return 0;
    }

    return data.address() ? hash_data(data.get(mem), texture_size) : 0;
}

// Function to hash an arbitrary swizzled texture in the most optimized way possible
// this is a recursive function which calls itself on the 4 higher block making the sizzle
// once a block entirely in the swizzle is found, it stops and hash it
// the pixels should be hash in the exact order they appear in the memory
static void hash_arbitrary_swizzled(const uint8_t *data, uint32_t width, uint32_t height, uint32_t texture_width, uint32_t texture_height, uint32_t texture_size, XXH3_state_t *hash_state) {
    if (width >= texture_width && height >= texture_height) {
        // whole block is included, hash it
        XXH3_64bits_update(hash_state, data, texture_size);
        return;
    }

    // divide the current block in 4 subblocks
    const uint32_t block_width = texture_width / 2;
    const uint32_t block_height = texture_height / 2;
    const uint32_t block_size = texture_size / 4;

    // we always hash the first subblock (it always contains something)
    hash_arbitrary_swizzled(data, width, height, block_width, block_height, block_size, hash_state);

    if (height > block_height) {
        hash_arbitrary_swizzled(data + block_size, width, height - block_height, block_width, block_height, block_size, hash_state);
    }

    if (width > block_width) {
        hash_arbitrary_swizzled(data + 2 * block_size, width - block_width, height, block_width, block_height, block_size, hash_state);
    }

    if (height > block_height && width > block_width) {
        hash_arbitrary_swizzled(data + 3 * block_size, width - block_width, height - block_height, block_width, block_height, block_size, hash_state);
    }
}

// hash only the visible pixels of a tiled texture
static void hash_unaligned_tiled(const uint8_t *data, uint32_t width, uint32_t height, uint32_t block_width, uint32_t block_height, uint32_t bpp, XXH3_state_t *hash_state) {
    // a tile is 32x32
    constexpr uint32_t tile_mask = 0x1F;

    const uint32_t width_down_aligned = align_down(width, 32);
    const uint32_t height_down_aligned = align_down(height, 32);

    // we need to take blocks into account, so consider a block line as
    // a 32xblock_height rectangle of pixels
    const uint32_t tile_block_lines = 32 / block_height;
    const uint32_t total_line_size = (32 * block_height * bpp) / 8;

    if (width == width_down_aligned) {
        // just hash everything (except the bottom) in one go
        const uint32_t hash_size = (width * height_down_aligned * bpp) / 8;
        XXH3_64bits_update(hash_state, data, hash_size);
        data += hash_size;
    } else {
        // need to hash block lines one by one
        const uint32_t block_lines = height_down_aligned / 32;
        const uint32_t filled_line_size = (width_down_aligned * 32 * bpp) / 8;
        const uint32_t end_line_used = ((width & tile_mask) * block_height * bpp) / 8;

        // we are only using the left of the tiles
        for (uint32_t block_line = 0; block_line < block_lines; block_line++) {
            XXH3_64bits_update(hash_state, data, filled_line_size);
            data += filled_line_size;

            for (uint32_t tile_block_line = 0; tile_block_line < tile_block_lines; tile_block_line++) {
                XXH3_64bits_update(hash_state, data, end_line_used);
                data += total_line_size;
            }
        }
    }

    if (height == height_down_aligned)
        // we are done
        return;

    // we are only using the top of the tiles
    const uint32_t tile_size = (32 * 32 * bpp) / 8;
    const uint32_t used_tile_size = (32 * (height & tile_mask) * bpp) / 8;
    const uint32_t nb_tiles_x = width_down_aligned / 32;
    for (uint32_t tile_x = 0; tile_x < nb_tiles_x; tile_x++) {
        XXH3_64bits_update(hash_state, data, used_tile_size);
        data += tile_size;
    }

    if (width == width_down_aligned)
        // we are done
        return;

    // We are only using the top left part of the bottom right tile
    const uint32_t end_line_used = ((width & tile_mask) * block_height * bpp) / 8;
    const uint32_t nb_lines_used = (height & tile_mask) / block_height;
    for (uint32_t line = 0; line < nb_lines_used; line++) {
        XXH3_64bits_update(hash_state, data, end_line_used);
        data += total_line_size;
    }
}

uint64_t hash_texture_nostride(const SceGxmTexture &texture, const MemState &mem) {
    const SceGxmTextureFormat format = gxm::get_format(texture);
    const SceGxmTextureBaseFormat base_format = gxm::get_base_format(format);
    const Ptr<const uint8_t> data(texture.data_addr << 2);

    if (!data)
        return 0;

    uint32_t width = gxm::get_width(texture);
    uint32_t height = gxm::get_height(texture);
    auto [block_width, block_height] = gxm::get_block_size(base_format);
    width = align(width, block_width);
    height = align(height, block_height);

    // put the width and height of the texture in the hash
    // also put the gamma mode in case the same texture is used with and without srgb
    // and put the swizzle, although it's only used for textures with 3 or more components
    uint64_t hash = width << 16
        | height
        | static_cast<uint64_t>(texture.gamma_mode != 0) << 32
        | static_cast<uint64_t>(texture.swizzle_format) << 33;

    // handle paletted texture (create one hash for each variant)
    switch (base_format) {
    case SCE_GXM_TEXTURE_BASE_FORMAT_P4:
        hash ^= hash_texture_palette(texture, mem);
        // update the block dimensions in this case (needed for hashing latter)
        block_width = 2;
        block_height = 1;
        break;
    case SCE_GXM_TEXTURE_BASE_FORMAT_P8:
        hash ^= hash_texture_palette(texture, mem);
        break;
    default:
        break;
    }

    // special case
    if (gxm::is_yuv_format(base_format)) {
        hash ^= hash_data(data.get(mem), gxm::texture_size_first_mip(texture));
        return hash;
    }

    const uint32_t bpp = gxm::bits_per_pixel(base_format);

    // if there is no pixel in the stride, we can just hash the whole texture
    bool has_no_stride = true;
    uint32_t stride_in_pixels = width;
    switch (texture.texture_type()) {
    case SCE_GXM_TEXTURE_LINEAR_STRIDED:
        stride_in_pixels = (gxm::get_stride_in_bytes(texture) * 8) / bpp;
        has_no_stride = stride_in_pixels == width;
        break;
    case SCE_GXM_TEXTURE_LINEAR:
        stride_in_pixels = align(width, 8);
        has_no_stride = stride_in_pixels == width;
        break;
    case SCE_GXM_TEXTURE_SWIZZLED_ARBITRARY:
    case SCE_GXM_TEXTURE_CUBE_ARBITRARY:
        // it has no stride if both width and height are powers of 2
        has_no_stride = (width & (width - 1)) == 0 && (height & (height - 1)) == 0;
        break;
    case SCE_GXM_TEXTURE_TILED:
        // it has no stride if both the width and height are multiple of the tile size (32)
        stride_in_pixels = align(width, 32);
        has_no_stride = (width & 0x1F) == 0 && (height & 0x1F) == 0;
        break;
    default:
        break;
    }

    if (has_no_stride) {
        // just hash the whole first mips and we are done
        // perform the computation with 64-bit integers for safety
        // I checked, width * height * bpp will always fit in a 32-bit unsigned integer
        uint32_t texture_size = (width * height * bpp) / 8;
        hash ^= hash_data(data.get(mem), texture_size);
        return hash;
    }

    // all the pixels are not in a contiguous memory range
    static XXH3_state_t *hash_state = XXH3_createState();
    XXH3_64bits_reset(hash_state);

    if (texture.texture_type() == SCE_GXM_TEXTURE_LINEAR || texture.texture_type() == SCE_GXM_TEXTURE_LINEAR_STRIDED) {
        // hash line by line
        // need to take block compressed textures into account
        uint32_t block_stride_in_bytes = (stride_in_pixels * block_height * bpp) / 8;
        uint32_t block_width_in_bytes = (width * block_height * bpp) / 8;
        uint32_t nb_blocks_y = height / block_height;
        const uint8_t *data_loc = data.get(mem);
        for (uint32_t block_y = 0; block_y < nb_blocks_y; block_y++) {
            XXH3_64bits_update(hash_state, data_loc, block_width_in_bytes);
            data_loc += block_stride_in_bytes;
        }

        hash ^= XXH3_64bits_digest(hash_state);
        return hash;
    }

    if (texture.texture_type() == SCE_GXM_TEXTURE_TILED) {
        // some side tiles have non-used pixels
        hash_unaligned_tiled(data.get(mem), width, height, block_width, block_height, bpp, hash_state);

        hash ^= XXH3_64bits_digest(hash_state);
        return hash;
    }

    // texture is arbitrarily swizzled
    // hash completely used block by completely used block
    const uint32_t texture_width = next_power_of_two(width);
    const uint32_t texture_height = next_power_of_two(height);
    const uint32_t texture_size = (texture_width * texture_height * bpp) / 8;
    hash_arbitrary_swizzled(data.get(mem), width, height, texture_width, texture_height, texture_size, hash_state);
    hash ^= XXH3_64bits_digest(hash_state);
    return hash;
}

uint16_t get_upload_mip(const uint16_t true_mip, const uint16_t width, const uint16_t height) {
    uint16_t max_mip_text = std::bit_width(std::min(width, height));
    return std::min(true_mip, max_mip_text);
}
} // namespace texture

using namespace texture;

bool TextureCache::init(const fs::path &texture_folder, std::string_view game_id, const size_t sampler_cache_size) {
    // write-trap tracking is always on
    // initialize the texture queue
    texture_queue.init(TextureCacheSize);
    // set the proper index of each entry
    for (size_t i = 0; i < TextureCacheSize; i++)
        texture_queue.items[i].content.index = static_cast<int>(i);

    // prevent stutter caused by the hashmap resizing
    texture_lookup.reserve(TextureCacheSize);

    use_sampler_cache = sampler_cache_size > 0;
    if (use_sampler_cache) {
        sampler_queue.init(sampler_cache_size);

        for (size_t i = 0; i < sampler_cache_size; i++)
            sampler_queue.items[i].content.index = static_cast<int>(i);

        sampler_lookup.reserve(sampler_cache_size);
    }

    export_folder = texture_folder / "export" / std::string(game_id);
    import_folder = texture_folder / "import" / std::string(game_id);

    refresh_available_textures();

    return true;
}

void TextureCache::upload_texture(const SceGxmTexture &gxm_texture, MemState &mem, const texture_policy::RowRun *rows) {
    R_PROFILE(__func__);

    bool is_vulkan = (backend == renderer::Backend::Vulkan);

    const SceGxmTextureFormat fmt = gxm::get_format(gxm_texture);
    const SceGxmTextureBaseFormat base_format = gxm::get_base_format(fmt);

    uint32_t width = gxm::get_width(gxm_texture);
    uint32_t height = gxm::get_height(gxm_texture);

    const Ptr<uint8_t> data(gxm_texture.data_addr << 2);
    uint8_t *texture_data = data.get(mem);

    if (!texture_data) {
        LOG_ERROR_ONCE("Texture data at 0x{:X} is not mapped ({}x{} format 0x{:X}) - keeping the previous texture content", gxm_texture.data_addr << 2, width, height, fmt::underlying(fmt));
        return;
    }

    std::vector<uint8_t> texture_data_decompressed;
    std::vector<uint8_t> texture_pixels_lineared;

    const void *pixels = nullptr;

    uint32_t pixels_per_stride = 0;
    uint32_t bpp = gxm::bits_per_pixel(base_format);
    uint32_t bytes_per_pixel = (bpp + 7) >> 3;

    const auto texture_type = gxm_texture.texture_type();
    const bool is_swizzled = (texture_type == SCE_GXM_TEXTURE_SWIZZLED) || (texture_type == SCE_GXM_TEXTURE_CUBE) || (texture_type == SCE_GXM_TEXTURE_SWIZZLED_ARBITRARY) || (texture_type == SCE_GXM_TEXTURE_CUBE_ARBITRARY);

    uint32_t mip_index = 0;
    uint32_t total_mip = get_upload_mip(gxm_texture.true_mip_count(), width, height);
    uint32_t face_uploaded_count = 0;
    uint32_t face_total_count;
    uint32_t total_source_so_far = 0;

    // Modified during decompression
    const uint32_t org_width = width;
    const uint32_t org_height = height;

    uint32_t face_align_bytes = 4;

    // > 0 means texture cube
    int upload_type = 0;

    face_total_count = 1;
    if (texture_type == SCE_GXM_TEXTURE_CUBE || texture_type == SCE_GXM_TEXTURE_CUBE_ARBITRARY) {
        upload_type = 1;
        face_total_count = 6;

        if (gxm_texture.mip_count != 0xF) {
            const bool twok_align_cond1 = width >= 32 && height >= 32 && (bpp <= 8 || gxm::is_block_compressed_format(base_format));
            const bool twok_align_cond2 = width >= 16 && height >= 16 && (bpp == 16 || bpp == 32);
            const bool twok_align_cond3 = width >= 8 && height >= 8 && bpp == 64;

            if (twok_align_cond1 || twok_align_cond2 || twok_align_cond3) {
                face_align_bytes = 2048;
            }
        }
    }

    uint32_t layout_width;
    uint32_t layout_height;
    if (gxm_texture.mip_count == 0xF && texture_type == SCE_GXM_TEXTURE_LINEAR) {
        // a mipcount of 0xF means no mips, so for cube and planes, they follow each other directly without padding
        layout_width = width;
        layout_height = height;
    } else {
        layout_width = next_power_of_two(width);
        layout_height = next_power_of_two(height);
    }
    auto [block_width, block_height] = gxm::get_block_size(base_format);
    // block size in bytes
    const uint32_t block_size = (block_width * block_height * bpp) / 8;
    // from the number of pixels in a mipmap, we can get the number of blocks by shifting to the right by block_shift
    const uint32_t block_shift = std::bit_width(block_width * block_height) - 1;

    uint32_t align_width = block_width;
    uint32_t align_height = block_height;
    if (texture_type == SCE_GXM_TEXTURE_LINEAR) {
        align_width = std::max(align_width, 8U);
    } else if (texture_type == SCE_GXM_TEXTURE_TILED) {
        align_width = std::max(align_width, 32U);
        align_height = std::max(align_height, 32U);
    }

    const uint32_t org_layout_width = layout_width;
    const uint32_t org_layout_height = layout_height;

    // a row range only exists for the single level of a linear texture (the caller planned it from the same layout)
    if (rows && (total_mip != 1 || face_total_count != 1 || !partial_upload || rows->row_end <= rows->row_begin))
        rows = nullptr;
    if (!rows)
        upload_row_offset = 0;

    while (face_uploaded_count < face_total_count && org_width > 0 && org_height > 0) {
        pixels = texture_data;

        SceGxmTextureBaseFormat upload_format = base_format;
        uint32_t memory_height = height;

        // Get pixels per stride
        pixels_per_stride = width;
        switch (texture_type) {
        case SCE_GXM_TEXTURE_SWIZZLED_ARBITRARY:
        case SCE_GXM_TEXTURE_CUBE_ARBITRARY:
            pixels_per_stride = next_power_of_two(width);
            memory_height = next_power_of_two(height);
            break;
        case SCE_GXM_TEXTURE_LINEAR_STRIDED:
            pixels_per_stride = gxm::get_stride_in_bytes(gxm_texture) / bytes_per_pixel;
            if (base_format == SCE_GXM_TEXTURE_BASE_FORMAT_P4) // P4 textures are the only one not byte aligned, therefore bytes_per_pixel should be 0.5 and not 1, correct it here
                pixels_per_stride *= 2;
            break;
        default:
            break;
        }
        pixels_per_stride = align(pixels_per_stride, align_width);
        memory_height = align(memory_height, align_height);

        // only the rows [row_begin, row_end) of the first level: start at their source row and convert/upload just them
        uint32_t upload_height = height;
        if (rows) {
            const uint32_t block_row_bytes = (pixels_per_stride * block_height * bpp) / 8;
            pixels = texture_data + (rows->row_begin / block_height) * block_row_bytes;
            memory_height = rows->row_end - rows->row_begin;
            upload_height = std::min(rows->row_end, height) - std::min(rows->row_begin, height);
            upload_row_offset = rows->row_begin;
            if (upload_height == 0)
                break;
        }

        // A queued bind can be processed after the game released the staging memory, and reading it crashes on decommitted.
        {
            const uint32_t src_read_size = (pixels_per_stride * memory_height * gxm::bits_per_pixel(base_format)) / 8;
            const Address src_address = (gxm_texture.data_addr << 2) + total_source_so_far;
            if (src_read_size > 0 && !is_valid_addr_range(mem, src_address, src_address + src_read_size)) {
                LOG_WARN("Texture upload source is not in valid memory (addr=0x{:08X} size={}), skipping upload", src_address, src_read_size);
                break;
            }
        }

        // perform all needed conversions (formats not supported by modern GPUs)
        switch (base_format) {
        case SCE_GXM_TEXTURE_BASE_FORMAT_P4:
        case SCE_GXM_TEXTURE_BASE_FORMAT_P8:
            texture_data_decompressed.resize(pixels_per_stride * memory_height * 4);
            if (base_format == SCE_GXM_TEXTURE_BASE_FORMAT_P8) {
                palette_texture_to_rgba_8(reinterpret_cast<uint32_t *>(texture_data_decompressed.data()),
                    static_cast<const uint8_t *>(pixels), pixels_per_stride, memory_height, get_texture_palette(gxm_texture, mem));
            } else {
                palette_texture_to_rgba_4(reinterpret_cast<uint32_t *>(texture_data_decompressed.data()),
                    static_cast<const uint8_t *>(pixels), pixels_per_stride, memory_height, get_texture_palette(gxm_texture, mem));
            }
            pixels = texture_data_decompressed.data();
            bytes_per_pixel = 4;
            bpp = 32;
            upload_format = SCE_GXM_TEXTURE_BASE_FORMAT_U8U8U8U8;
            break;
        case SCE_GXM_TEXTURE_BASE_FORMAT_PVRT2BPP:
        case SCE_GXM_TEXTURE_BASE_FORMAT_PVRT4BPP:
        case SCE_GXM_TEXTURE_BASE_FORMAT_PVRTII2BPP:
        case SCE_GXM_TEXTURE_BASE_FORMAT_PVRTII4BPP:
            if (support_pvrt) {
                LOG_INFO_ONCE("Your device support SCE_GXM_TEXTURE_BASE_FORMAT_PVRT");
                break;
            }
            if (!is_swizzled)
                LOG_ERROR_ONCE("Unhandled non-swizzled PVRT format, please report it to the developers");

            texture_data_decompressed.resize(pixels_per_stride * memory_height * 4);
            // this actually also unswizzles the texture
            decompress_compressed_texture(base_format, texture_data_decompressed.data(), pixels, pixels_per_stride, memory_height);
            bytes_per_pixel = 4;
            bpp = 32;
            upload_format = SCE_GXM_TEXTURE_BASE_FORMAT_U8U8U8U8;
            pixels = texture_data_decompressed.data();
            break;
        case SCE_GXM_TEXTURE_BASE_FORMAT_U8U3U3U2:
            // Convert U8U3U3U2 to U8U8U8U8
            texture_data_decompressed.resize(pixels_per_stride * memory_height * 4);
            convert_U8U3U3U2_to_U8U8U8U8(texture_data_decompressed.data(), pixels, pixels_per_stride, memory_height);
            pixels = texture_data_decompressed.data();
            upload_format = SCE_GXM_TEXTURE_BASE_FORMAT_U8U8U8U8;
            bpp = 32;
            break;
        case SCE_GXM_TEXTURE_BASE_FORMAT_SE5M9M9M9:
            // this format is supported on all GPUs with vulkan
            if (is_vulkan && support_e5rgb9) {
                LOG_INFO_ONCE("Your device support SCE_GXM_TEXTURE_BASE_FORMAT_SE5M9M9M9");
                break;
            }
            texture_data_decompressed.resize(pixels_per_stride * memory_height * 6);
            decompress_packed_float_e5m9m9m9(base_format, texture_data_decompressed.data(), pixels, width, memory_height);
            pixels = texture_data_decompressed.data();
            break;
        case SCE_GXM_TEXTURE_BASE_FORMAT_U2F10F10F10:
            // don't change what openGL is doing (which is completely wrong)
            if (!is_vulkan || support_a2rgb10) {
                LOG_INFO_ONCE("Your device support SCE_GXM_TEXTURE_BASE_FORMAT_U2F10F10F10");
                break;
            }
            texture_data_decompressed.resize(pixels_per_stride * memory_height * 8);
            convert_u2f10f10f10_to_f16f16f16f16(texture_data_decompressed.data(), pixels, pixels_per_stride, memory_height, fmt);
            pixels = texture_data_decompressed.data();
            upload_format = SCE_GXM_TEXTURE_BASE_FORMAT_F16F16F16F16;
            break;
        case SCE_GXM_TEXTURE_BASE_FORMAT_X8U24:
            texture_data_decompressed.resize(pixels_per_stride * memory_height * 4);
            if (is_vulkan && support_x8d24) {
                LOG_INFO_ONCE("Your device support SCE_GXM_TEXTURE_BASE_FORMAT_X8U24");
                break;
            } else if (is_vulkan) {
                // d24_u8 or x8_d24 is not supported on all GPUs (thanks AMD)
                convert_x8u24_to_f32(texture_data_decompressed.data(), pixels, pixels_per_stride, memory_height, fmt);
                upload_format = SCE_GXM_TEXTURE_BASE_FORMAT_F32;
            } else {
                // X8 = [24-31], D24 = [0-23], technically this is GL_UNSIGNED_INT_24_8_REV which does not exist
                // TODO: Requires shader to convert the normalized value read by GL to unsigned int. Just multiply by 2^24-1 when reading and you're done.
                // TODO: this is wrong, the depth is in the upper or lower 24 bits according to the swizzle
                convert_x8u24_to_u24x8(texture_data_decompressed.data(), pixels, pixels_per_stride, memory_height);
            }
            pixels = texture_data_decompressed.data();
            break;
        case SCE_GXM_TEXTURE_BASE_FORMAT_F32M:
            // Convert F32M to F32
            texture_data_decompressed.resize(pixels_per_stride * memory_height * 4);
            convert_f32m_to_f32(texture_data_decompressed.data(), pixels, pixels_per_stride, memory_height);
            pixels = texture_data_decompressed.data();
            upload_format = SCE_GXM_TEXTURE_BASE_FORMAT_F32;
            break;
        case SCE_GXM_TEXTURE_BASE_FORMAT_YUV420P2:
        case SCE_GXM_TEXTURE_BASE_FORMAT_YUV420P3: {
            const uint32_t swizzle = gxm_texture.swizzle_format;
            const yuv::PlaneLayout420 planes = yuv::plane_layout_420(base_format == SCE_GXM_TEXTURE_BASE_FORMAT_YUV420P3,
                pixels_per_stride, memory_height, pixels_per_stride, layout_width, layout_height);
            const Address plane_address = (gxm_texture.data_addr << 2) + total_source_so_far;
            if (!is_valid_addr_range(mem, plane_address, plane_address + planes.total_bytes)) {
                LOG_WARN("YUV texture planes are not in valid memory (addr=0x{:08X} size={}), skipping upload", plane_address, planes.total_bytes);
                return;
            }
            // GPU conversion is tried once per texture, on the first mip (a YUV texture has no usable mip chain)
            if (mip_index == 0 && upload_yuv_gpu(gxm_texture, base_format, static_cast<const uint8_t *>(pixels), width, height,
                    pixels_per_stride, layout_width, layout_height))
                return;
            texture_data_decompressed.resize(pixels_per_stride * memory_height * 4);
            yuv420_texture_to_rgb(gxm_texture.unk2, yuv::chroma_swapped_420(swizzle), texture_data_decompressed.data(),
                static_cast<const uint8_t *>(pixels), pixels_per_stride, memory_height, layout_width, layout_height,
                base_format == SCE_GXM_TEXTURE_BASE_FORMAT_YUV420P3);
            pixels = texture_data_decompressed.data();
            bpp = 32;
            upload_format = SCE_GXM_TEXTURE_BASE_FORMAT_U8U8U8U8;
            break;
        }
        case SCE_GXM_TEXTURE_BASE_FORMAT_YUV422:
            // converted below, once the texture has been brought to a linear layout
            if (mip_index == 0 && upload_yuv_gpu(gxm_texture, base_format, static_cast<const uint8_t *>(pixels), width, height,
                    pixels_per_stride, layout_width, layout_height))
                return;
            break;
        default:
            break;
        }

        if (texture_type != SCE_GXM_TEXTURE_LINEAR && texture_type != SCE_GXM_TEXTURE_LINEAR_STRIDED && !gxm::is_pvrt_format(base_format)) {
            // Convert data to linear layout
            texture_pixels_lineared.resize(pixels_per_stride * memory_height * bytes_per_pixel);

            if (is_swizzled && gxm::is_bcn_format(base_format))
                // just unswizzle the blocks
                resolve_z_order_compressed_texture(base_format, texture_pixels_lineared.data(), pixels, pixels_per_stride, memory_height);
            else if (is_swizzled)
                swizzled_texture_to_linear_texture(texture_pixels_lineared.data(), static_cast<const uint8_t *>(pixels), pixels_per_stride, memory_height,
                    static_cast<std::uint8_t>(bpp));
            else
                tiled_texture_to_linear_texture(texture_pixels_lineared.data(), static_cast<const uint8_t *>(pixels), pixels_per_stride, memory_height,
                    static_cast<std::uint8_t>(bpp));

            pixels = texture_pixels_lineared.data();
        }

        if (base_format == SCE_GXM_TEXTURE_BASE_FORMAT_YUV422) {
            texture_data_decompressed.resize(pixels_per_stride * memory_height * 4);
            yuv422_texture_to_rgb(gxm_texture.unk2, gxm_texture.swizzle_format, texture_data_decompressed.data(),
                static_cast<const uint8_t *>(pixels), pixels_per_stride, memory_height);
            pixels = texture_data_decompressed.data();
            bpp = 32;
            upload_format = SCE_GXM_TEXTURE_BASE_FORMAT_U8U8U8U8;
        }

        if (!support_dxt && gxm::is_bcn_format(base_format)) {
            // decompress the texture
            const int num_comp = gxm::get_num_components(base_format);
            texture_data_decompressed.resize(pixels_per_stride * memory_height * num_comp);
            decompress_compressed_texture(base_format, texture_data_decompressed.data(), pixels, pixels_per_stride, memory_height);
            pixels = texture_data_decompressed.data();
            bpp = num_comp * 8;
            upload_format = get_matching_decompressed_format(base_format);
        }

        const void *upload_pixels = pixels;
        if (base_format == SCE_GXM_TEXTURE_BASE_FORMAT_U1U5U5U5 && is_u5u5u5u1(fmt) == is_vulkan) {
            // Vulkan uploads A1R5G5B5 (1-bit field in bit 15, always supported), OpenGL uses
            // GL_UNSIGNED_SHORT_5_5_5_1 (1-bit field in bit 0, also available in GLES).
            // Only the upload is converted, exports keep the guest layout.
            texture_data_decompressed.resize(pixels_per_stride * memory_height * 2);
            if (is_vulkan)
                convert_u5u5u5u1_to_u1u5u5u5(texture_data_decompressed.data(), pixels, pixels_per_stride, memory_height);
            else
                convert_u1u5u5u5_to_u5u5u5u1(texture_data_decompressed.data(), pixels, pixels_per_stride, memory_height);
            upload_pixels = texture_data_decompressed.data();
        }

        upload_texture_impl(upload_format, width, upload_height, mip_index, upload_pixels, upload_type, pixels_per_stride);
        if (export_textures)
            export_texture_impl(upload_format, width, height, mip_index, pixels, upload_type, pixels_per_stride);

        const uint32_t nb_pixels = align(layout_width, align_width) * align(layout_height, align_height);
        const uint32_t mip_size = (nb_pixels >> block_shift) * block_size;
        texture_data += mip_size;
        total_source_so_far += mip_size;

        mip_index++;
        width /= 2;
        height /= 2;
        layout_width /= 2;
        layout_height /= 2;

        if (mip_index == total_mip) {
            if ((texture_type == SCE_GXM_TEXTURE_CUBE || texture_type == SCE_GXM_TEXTURE_CUBE_ARBITRARY) && gxm_texture.mip_count != 0xF) {
                // we must do as if all possible mips are here
                while (layout_width > 0 && layout_height > 0) {
                    const uint32_t nb_pixels = align(layout_width, align_width) * align(layout_height, align_height);
                    const uint32_t mip_size = (nb_pixels >> block_shift) * block_size;
                    texture_data += mip_size;
                    total_source_so_far += mip_size;
                    layout_width /= 2;
                    layout_height /= 2;
                }
            }

            mip_index = 0;
            face_uploaded_count++;

            layout_width = org_layout_width;
            layout_height = org_layout_height;
            width = org_width;
            height = org_height;

            upload_type++;

            uint32_t source_unaligned_size = total_source_so_far;
            total_source_so_far = align(total_source_so_far, face_align_bytes);

            texture_data += total_source_so_far - source_unaligned_size;
        }
    }
}

// remove everything related to the sampler state
// word 0 keeps bits 29-30 (SceGxmTexture::unk2): for YUV textures the renderer stores the colour space conversion
// profile there (see renderer::set_texture), and the same data must not be shared between two profiles
static constexpr TextureGxmDataRepr default_texture_mask = {
    0xF81E0000,
    0xFFFFFFFF,
    0xFFFFFFFC,
    0xF3FFFFFF
};
static constexpr TextureGxmDataRepr strided_texture_mask = {
    0xFFFE0E06,
    0xFFFFFFFF,
    0xFFFFFFFC,
    0xF3FFFFFF
};

// ---------------------------------------------------------------------------------------------
// Write-trap tracking (policy: texture_policy.h)
// ---------------------------------------------------------------------------------------------

// Watches the host pages covering [address, address + bytes) for writes. The trap is one-shot and also fires for
// reads of a merged read-protected neighbour (the surface cache): "fired" only means "judge the bytes by hash".
static std::shared_ptr<TextureTrap> arm_write_trap(MemState &mem, Address address, uint32_t bytes) {
    const texture_policy::PageSpan span = texture_policy::trap_span(address, bytes, KiB(4));
    std::shared_ptr<TextureTrap> trap = std::make_shared<TextureTrap>();
    add_protect(mem, span.begin, span.end - span.begin, MemPerm::ReadOnly, [trap](Address, bool) {
        trap->fired.store(true);
        return true;
    });
    return trap;
}

static bool trap_has_fired(MemState &mem, const std::shared_ptr<TextureTrap> &trap) {
    if (!trap || !trap->fired.load())
        return false;
    // the fault handler flags the trap before it opens the pages: wait until it has finished, so the bytes
    // hashed next are read after the access that tripped the trap (a still-pending write faults on the re-armed pages)
    { const std::lock_guard<std::mutex> handler_done(mem.protect_mutex); }
    return true;
}

void TextureCache::reset_tracking(TextureCacheInfo &info) {
    info.pixel_trap.reset();
    info.palette_trap.reset();
    info.chunk_hashes.clear();
    info.palette_hash = 0;
    info.chunk_plan = {};
    info.total_rows = 0;
    info.block_rows = 1;
    info.row_pitch_bytes = 0;
    info.partial_capable = false;
    info.pixel_epoch = 0;
    info.palette_epoch = 0;
    info.validated_scene = 0;
    info.dirty = false;
}

static bool is_paletted_base(SceGxmTextureBaseFormat base_format) {
    return base_format == SCE_GXM_TEXTURE_BASE_FORMAT_P4 || base_format == SCE_GXM_TEXTURE_BASE_FORMAT_P8;
}

// Row layout of the first level as upload_texture reads it, for linear textures that can be uploaded by rows.
static void plan_entry_layout(TextureCacheInfo &info, const SceGxmTexture &gxm_texture, const bool partial_allowed) {
    info.chunk_plan = {};
    info.total_rows = 0;
    info.block_rows = 1;
    info.row_pitch_bytes = 0;
    info.partial_capable = false;

    const SceGxmTextureBaseFormat base_format = gxm::get_base_format(gxm::get_format(gxm_texture));
    const SceGxmTextureType type = gxm_texture.texture_type();
    if (!partial_allowed || (type != SCE_GXM_TEXTURE_LINEAR && type != SCE_GXM_TEXTURE_LINEAR_STRIDED))
        return;
    if (gxm::is_yuv_format(base_format) || gxm::is_pvrt_format(base_format))
        return;

    const uint32_t width = gxm::get_width(gxm_texture);
    const uint32_t height = gxm::get_height(gxm_texture);
    if (width == 0 || height == 0 || get_upload_mip(gxm_texture.true_mip_count(), width, height) != 1)
        return;

    const auto [block_width, block_height] = gxm::get_block_size(base_format);
    const uint32_t bpp = gxm::bits_per_pixel(base_format);
    // same stride rules as upload_texture
    uint32_t pixels_per_stride = width;
    uint32_t align_width = block_width;
    if (type == SCE_GXM_TEXTURE_LINEAR_STRIDED) {
        pixels_per_stride = gxm::get_stride_in_bytes(gxm_texture) / ((bpp + 7) >> 3);
        if (base_format == SCE_GXM_TEXTURE_BASE_FORMAT_P4)
            pixels_per_stride *= 2;
    } else {
        align_width = std::max(align_width, 8U);
    }
    pixels_per_stride = align(pixels_per_stride, align_width);

    const uint32_t total_rows = align(height, block_height);
    const uint32_t row_pitch_bytes = (pixels_per_stride * block_height * bpp) / 8;
    const uint64_t rows_bytes = static_cast<uint64_t>(row_pitch_bytes) * (total_rows / block_height);
    if (row_pitch_bytes == 0 || rows_bytes > info.texture_size)
        return;

    info.total_rows = total_rows;
    info.block_rows = block_height;
    info.row_pitch_bytes = row_pitch_bytes;
    info.partial_capable = true;
    info.chunk_plan = texture_policy::plan_chunks(true, static_cast<uint32_t>(rows_bytes), row_pitch_bytes, total_rows, block_height);
}

// Hashes of the texture bytes: one per chunk of a big linear texture, otherwise a single one.
static void hash_entry_chunks(const TextureCacheInfo &info, const SceGxmTexture &gxm_texture, const MemState &mem, std::vector<uint64_t> &out) {
    out.clear();
    if (info.chunk_plan.rows_per_chunk == 0) {
        out.push_back(hash_texture_pixels(gxm_texture, info.texture_size, mem));
        return;
    }
    const Address base = gxm_texture.data_addr << 2;
    const uint64_t rows_bytes = static_cast<uint64_t>(info.row_pitch_bytes) * (info.total_rows / info.block_rows);
    if (!is_valid_addr_range(mem, base, base + static_cast<Address>(rows_bytes))) {
        LOG_WARN_ONCE("Texture data not in valid memory (addr=0x{:08X} size={}), not hashing", base, rows_bytes);
        out.assign(info.chunk_plan.chunk_count, 0);
        return;
    }
    const uint8_t *data = Ptr<const uint8_t>(base).get(mem);
    const uint64_t chunk_bytes = static_cast<uint64_t>(info.chunk_plan.rows_per_chunk / info.block_rows) * info.row_pitch_bytes;
    for (uint32_t i = 0; i < info.chunk_plan.chunk_count; i++) {
        const uint64_t begin = i * chunk_bytes;
        out.push_back(hash_data(data + begin, std::min(chunk_bytes, rows_bytes - begin)));
    }
}

// Arms the write trap over the texture bytes (before they are hashed), and hashes them into `hashes`.
static void arm_and_hash_pixels(TextureCacheInfo &info, const SceGxmTexture &gxm_texture, MemState &mem, std::vector<uint64_t> &hashes) {
    const Address base = gxm_texture.data_addr << 2;
    // the trap goes first: a write after it fires the trap, a write before it is seen by the hash
    info.pixel_epoch = release_epoch_now(mem);
    if (base && info.texture_size && is_valid_addr_range(mem, base, base + info.texture_size))
        info.pixel_trap = arm_write_trap(mem, base, info.texture_size);
    else
        info.pixel_trap.reset();
    hash_entry_chunks(info, gxm_texture, mem, hashes);
}

static void arm_and_hash_palette(TextureCacheInfo &info, const SceGxmTexture &gxm_texture, MemState &mem, uint64_t &hash) {
    const SceGxmTextureBaseFormat base_format = gxm::get_base_format(gxm::get_format(gxm_texture));
    const Address address = static_cast<Address>(gxm_texture.palette_addr) << 6;
    const uint32_t bytes = static_cast<uint32_t>(palette_entry_count(base_format) * sizeof(uint32_t));
    info.palette_epoch = release_epoch_now(mem);
    if (address && is_valid_addr_range(mem, address, address + bytes))
        info.palette_trap = arm_write_trap(mem, address, bytes);
    else
        info.palette_trap.reset();
    hash = hash_texture_palette(gxm_texture, mem);
}

// (Re)builds every baseline of an entry that is about to be uploaded whole.
static void track_entry(TextureCacheInfo &info, const SceGxmTexture &gxm_texture, MemState &mem, const bool partial_allowed) {
    plan_entry_layout(info, gxm_texture, partial_allowed);
    arm_and_hash_pixels(info, gxm_texture, mem, info.chunk_hashes);
    if (is_paletted_base(gxm::get_base_format(gxm::get_format(gxm_texture))))
        arm_and_hash_palette(info, gxm_texture, mem, info.palette_hash);
}

// A bind of an entry that is already in the cache: reads guest memory only when a write trap says it may have
// changed. Returns what has to be uploaded; `runs` receives the row runs of a partial upload.
texture_policy::UploadKind TextureCache::revalidate_entry(TextureCacheInfo &info, const SceGxmTexture &gxm_texture, MemState &mem, std::vector<texture_policy::RowRun> &runs, bool &hashed) {
    using namespace texture_policy;
    runs.clear();
    hashed = false;

    const bool partial_allowed = supports_partial_upload() && !(import_textures || export_textures);

    if (info.dirty) {
        // forced (replacement toggled): rebuild all baselines with the upload
        track_entry(info, gxm_texture, mem, partial_allowed);
        hashed = true;
        info.dirty = false;
        info.validated_scene = current_scene;
        return UploadKind::Full;
    }

    const SceGxmTextureBaseFormat base_format = gxm::get_base_format(gxm::get_format(gxm_texture));
    const Address base = gxm_texture.data_addr << 2;

    EntryState state;
    state.validated_this_scene = current_scene != 0 && info.validated_scene == current_scene;
    state.pixels_armed = static_cast<bool>(info.pixel_trap);
    state.paletted = is_paletted_base(base_format);
    state.palette_armed = static_cast<bool>(info.palette_trap);
    if (!state.validated_this_scene) {
        state.gpu_may_write = gpu_may_write_guest_range(base, base + info.texture_size);
        state.pixels_fired = trap_has_fired(mem, info.pixel_trap);
        state.palette_fired = trap_has_fired(mem, info.palette_trap);
        // guest memory freed (and maybe reused for new data) since a trap was armed
        if (state.pixels_armed && released_since(mem, base, base + info.texture_size, info.pixel_epoch))
            state.memory_released = true;
        if (state.palette_armed) {
            const Address palette = static_cast<Address>(gxm_texture.palette_addr) << 6;
            if (released_since(mem, palette, palette + static_cast<Address>(palette_entry_count(base_format) * sizeof(uint32_t)), info.palette_epoch))
                state.memory_released = true;
        }
    }

    const Revalidation need = revalidation_needed(state);
    info.validated_scene = current_scene;
    if (need.skip)
        return UploadKind::None;

    HashOutcome outcome;
    outcome.total_rows = info.total_rows;
    outcome.partial_possible = partial_allowed && info.partial_capable && info.chunk_plan.rows_per_chunk != 0;

    std::vector<uint64_t> new_hashes;
    if (need.hash_pixels) {
        arm_and_hash_pixels(info, gxm_texture, mem, new_hashes);
        hashed = true;
        outcome.pixels_checked = true;
        if (outcome.partial_possible) {
            runs = changed_runs(info.chunk_hashes, new_hashes, info.chunk_plan, info.total_rows);
            outcome.pixels_changed = !runs.empty();
            outcome.dirty_rows = rows_in_runs(runs);
        } else {
            outcome.pixels_changed = info.chunk_hashes != new_hashes;
        }
        info.chunk_hashes = std::move(new_hashes);
    }
    if (need.hash_palette) {
        uint64_t palette_hash = 0;
        arm_and_hash_palette(info, gxm_texture, mem, palette_hash);
        hashed = true;
        outcome.palette_checked = true;
        outcome.palette_changed = palette_hash != info.palette_hash;
        info.palette_hash = palette_hash;
    }

    const UploadKind kind = decide_upload(outcome);
    if (kind != UploadKind::Partial)
        runs.clear();
    return kind;
}

void TextureCache::cache_and_bind_texture(const SceGxmTexture &gxm_texture, MemState &mem) {
    R_PROFILE(__func__);

    size_t index = 0;
    bool configure = false;
    bool upload = false;
    texture_policy::UploadKind upload_kind = texture_policy::UploadKind::None;
    bool hashed = false;
    std::vector<texture_policy::RowRun> partial_runs;

    // Try to find GXM texture in cache.
    int cached_gxm_texture_index = -1;
    TextureGxmDataRepr texture_repr = std::bit_cast<TextureGxmDataRepr>(gxm_texture);
    if (use_sampler_cache) {
        // remove the sampler state from the representation
        const TextureGxmDataRepr &mask = (gxm_texture.texture_type() == SCE_GXM_TEXTURE_LINEAR_STRIDED) ? strided_texture_mask : default_texture_mask;
        for (int i = 0; i < 4; i++)
            texture_repr[i] &= mask[i];
    }
    auto gxm_it = texture_lookup.find(texture_repr);
    if (gxm_it != texture_lookup.end())
        // we found the texture in the cache
        cached_gxm_texture_index = gxm_it->second->index;

    TextureCacheInfo *info;
    if (cached_gxm_texture_index == -1) {
        // Texture not found in cache.
        // get the least recently used texture, which info_list_head points to
        info = texture_queue.get_lru();
        index = info->index;
        if (info->texture_size > 0) {
            // Cache is full.
            LOG_WARN_ONCE("Texture cache is full. Starting to replace textures");
            texture_lookup.erase(std::bit_cast<TextureGxmDataRepr>(info->texture));
        }
        texture_lookup[texture_repr] = info;
        reset_tracking(*info);

        configure = true;
        upload = true;
        upload_kind = texture_policy::UploadKind::Full;
        // track the WHOLE mip chain, not just the first level
        info->texture_size = gxm::texture_size(gxm_texture);
        if (info->texture_size > 0 && !is_valid_addr_range(mem, gxm_texture.data_addr << 2, (gxm_texture.data_addr << 2) + info->texture_size))
            info->texture_size = gxm::texture_size_first_mip(gxm_texture);
        // use the texture_repr representation, it contains everything we need and we can use it to erase the key
        // from texture_lookup later
        info->texture = std::bit_cast<SceGxmTexture>(texture_repr);

        info->validated_scene = current_scene;
        if (gxm_texture.data_addr != 0) {
            track_entry(*info, gxm_texture, mem, supports_partial_upload() && !(import_textures || export_textures));
            hashed = true;
        }
    } else {
        // Texture is cached.
        index = cached_gxm_texture_index;
        info = gxm_it->second;
        configure = false;
        if (gxm_texture.data_addr != 0) {
            upload_kind = revalidate_entry(*info, gxm_texture, mem, partial_runs, hashed);
            upload = upload_kind != texture_policy::UploadKind::None;
        }
    }
    current_info = info;

    if (gxm_texture.data_addr == 0) {
        upload = false;
    }

    if (upload && (import_textures || export_textures)) {
        // the replacement textures are matched by the hash of the visible pixels
        info->hash = hash_texture_nostride(gxm_texture, mem);
    }

    importing_texture = false;
    if (upload && import_textures) {
        auto it = available_textures_hash.find(info->hash);
        if (it != available_textures_hash.end()) {
            importing_texture = true;
            loading_texture = it->second;
            // always configure for replacement texture (although it may have no effect)
            // the reason being that we may have two replacement textures for the same gxm identifier
            // with different dimensions, so we can't assume
            configure = true;
        }
    }

    if (upload && !importing_texture && info->is_imported)
        configure = true;

    select(index, gxm_texture);

    if (configure) {
        bool need_configure = true;

        if (importing_texture)
            need_configure = !import_configure_texture();

        if (need_configure) {
            configure_texture(gxm_texture);
            importing_texture = false;
            info->is_imported = false;
        }
    }
    if (upload) {
        if (export_textures && !importing_texture)
            export_select(gxm_texture);

        if (importing_texture) {
            import_upload_texture();
        } else if (upload_kind == texture_policy::UploadKind::Partial && !configure) {
            // only the changed row runs; the rest of the image is kept
            partial_upload = true;
            for (const texture_policy::RowRun &run : partial_runs)
                upload_texture(gxm_texture, mem, &run);
            partial_upload = false;
            upload_row_offset = 0;
        } else {
            upload_texture(gxm_texture, mem);
        }

        upload_done();
        if (export_textures && !importing_texture)
            export_done();
        if (importing_texture)
            import_done();
    }
#if ECV_ORACLE
    if (ecv::oracle::gpu::enabled()) {
        const Address oracle_addr = gxm_texture.data_addr << 2;
        const bool oracle_valid = upload && oracle_addr && is_valid_addr_range(mem, oracle_addr, oracle_addr + info->texture_size);
        ecv::oracle::gpu::tex_cache_result(upload, configure, hashed, info->texture_size, info->hash,
            oracle_valid ? hash_data(Ptr<const void>(oracle_addr).get(mem), info->texture_size) : 0);
    }
#endif
    importing_texture = false;

    // set the texture as the mru
    texture_queue.set_as_mru(info);

    // retrieve the appropriate sampler if needed
    if (use_sampler_cache)
        cache_and_bind_sampler(gxm_texture);
}

int TextureCache::cache_and_bind_sampler(const SceGxmTexture &gxm_texture, bool no_linear) {
    // Use the SGX-era point-sampling policy for full-width float texels. Games put
    // packed bytes in these words, so enabling a host-only filtering capability
    // changes their payloads. Include the effective policy in the cache key.
    const auto base_format = gxm::get_base_format(gxm::get_format(gxm_texture));
    no_linear |= base_format == SCE_GXM_TEXTURE_BASE_FORMAT_F32
        || base_format == SCE_GXM_TEXTURE_BASE_FORMAT_F32M
        || base_format == SCE_GXM_TEXTURE_BASE_FORMAT_F32F32;
    uint32_t compact_repr = 0;
    if (gxm_texture.texture_type() != SCE_GXM_TEXTURE_LINEAR_STRIDED) {
        compact_repr = 0b01
            | (gxm_texture.vaddr_mode << 2)
            | (gxm_texture.uaddr_mode << 5)
            | (gxm_texture.mip_filter << 8)
            | (gxm_texture.min_filter << 9)
            | (gxm_texture.mag_filter << 11)
            | (gxm_texture.lod_bias << 13)
            | (gxm_texture.lod_min0 << 19)
            | (gxm_texture.lod_min1 << 21);
    } else {
        // has a special representation
        compact_repr = 0b11
            | (gxm_texture.vaddr_mode << 2)
            | (gxm_texture.uaddr_mode << 5)
            | (gxm_texture.mag_filter << 8);
    }

    compact_repr |= (static_cast<uint32_t>(no_linear) << 23);
    // unnormalized (texel unit) coordinates change how the sampler is built
    compact_repr |= (static_cast<uint32_t>(gxm_texture.normalize_mode) << 24);

    auto it = sampler_lookup.find(compact_repr);
    if (it != sampler_lookup.end()) {
        sampler_queue.set_as_mru(it->second);
        last_bound_sampler_index = it->second->index;
        return last_bound_sampler_index;
    }

    // we didn't find a matching sampler, create a new one
    SamplerCacheInfo *info = sampler_queue.get_lru();
    if (info->value != 0) {
        // the compact representation can never be 0, so we can erase the previous value
        sampler_lookup.erase(info->value);
    }

    sampler_queue.set_as_mru(info);
    sampler_lookup[compact_repr] = info;

    info->value = compact_repr;
    configure_sampler(info->index, gxm_texture, no_linear);
    last_bound_sampler_index = info->index;
    return last_bound_sampler_index;
}

} // namespace renderer
