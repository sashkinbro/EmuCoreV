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

#pragma once

#include <gxm/types.h>
#include <renderer/texture_policy.h>
#include <util/containers.h>
#include <util/fs.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace ddspp {
struct Descriptor;
}

struct MemState;

enum SceGxmTextureBaseFormat : uint32_t;

namespace renderer {
enum class Backend : uint32_t;
static constexpr size_t TextureCacheSize = 1024;

typedef std::array<uint32_t, 4> TextureGxmDataRepr;
// Written by the memory-trap callback (fault thread), read by the render thread. A trap is one-shot: whatever
// the faulting access was, the pages are open afterwards, so "fired" only means "no longer watched".
// A fresh object is made every time the trap is armed, so a late callback of an old arming cannot mark the new one.
struct TextureTrap {
    std::atomic<bool> fired{ false };
};

struct TextureCacheInfo {
    // scene in which this texture's contents were last validated
    uint64_t validated_scene = 0;
    // hash of the visible pixels used to match replacement textures (only kept while importing/exporting)
    uint64_t hash = 0;
    SceGxmTexture texture;
    int index = 0;
    uint32_t texture_size = 0;
    // force a new upload on the next bind (replacement toggles), whatever the guest memory did
    bool dirty = false;
    // used for texture importation
    bool is_imported = false;
    bool is_srgb = false;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t mip_count = 0;
    SceGxmTextureBaseFormat format;

    // --- write-trap tracking (see texture_policy.h) ---
    // watches the pages of the texture bytes; null = not armed
    std::shared_ptr<TextureTrap> pixel_trap;
    // watches the pages of the P4/P8 palette; null = not armed
    std::shared_ptr<TextureTrap> palette_trap;
    // release epoch (mem::release_epoch_now) when each trap was armed
    uint64_t pixel_epoch = 0;
    uint64_t palette_epoch = 0;
    // hashes taken when the traps were armed: they are the baseline a fired trap is judged against
    std::vector<uint64_t> chunk_hashes;
    uint64_t palette_hash = 0;
    // layout of chunk_hashes (one chunk unless the texture is linear and big)
    texture_policy::ChunkPlan chunk_plan;
    // pixel rows in memory and bytes of one block row, valid when chunk_plan is chunked
    uint32_t total_rows = 0;
    uint32_t block_rows = 1;
    uint32_t row_pitch_bytes = 0;
    bool partial_capable = false;
};

struct SamplerCacheInfo {
    // compact representation of the sampler state
    uint32_t value = 0;
    int index = 0;
};

struct AvailableTexture {
    bool is_dds;
    std::shared_ptr<fs::path> folder_path;
};

class TextureCache {
protected:
    // current texture info the cache is looking at
    TextureCacheInfo *current_info = nullptr;

    // are we in the process of exporting a texture
    bool exporting_texture = false;
    // are we in the process of importing a texture
    bool importing_texture = false;

    // dds/png raw file
    std::vector<uint8_t> imported_texture_raw_data;
    // pointer to the decoded content
    uint8_t *imported_texture_decoded = nullptr;
    // Info about the texture currently loading
    AvailableTexture loading_texture;
    // contain the decrypted header when loading dds
    ddspp::Descriptor *dds_descriptor = nullptr;
    // file being written to when exporting dds
    fs::ofstream output_file;
    // used when exporting dds
    bool export_dds_swap_rb = false;

    bool import_textures = false;
    // if set to false, save textures as dds
    bool save_as_png = true;
    bool export_textures = false;

public:
    Backend backend;
    // set by upload_texture while it uploads a row range: the image content must be kept, and
    // upload_texture_impl writes the rows starting at upload_row_offset instead of row 0
    bool partial_upload = false;
    uint32_t upload_row_offset = 0;
    // Scene the renderer is currently replaying
    uint64_t current_scene = 0;
    // use a separate sampler cache
    bool use_sampler_cache = false;
    int anisotropic_filtering = 1;

    // used to quickly get the info from a hash of a gxm_texture
    unordered_map_fast<TextureGxmDataRepr, TextureCacheInfo *> texture_lookup;
    lru::Queue<TextureCacheInfo> texture_queue;

    // when use_sampler_cache is set to true, used to quickly get a cached sampler
    unordered_map_fast<uint32_t, SamplerCacheInfo *> sampler_lookup;
    lru::Queue<SamplerCacheInfo> sampler_queue;
    size_t last_bound_sampler_index;

    // folder where the replacement textures should be located
    fs::path import_folder;
    // key = hash, content = is the texture a dds (true) or a png (false)
    unordered_map_fast<uint64_t, AvailableTexture> available_textures_hash;

    // folder where the exported textures will be saved
    fs::path export_folder;
    // hash of the textures that have already been exported
    unordered_set_fast<uint64_t> exported_textures_hash;

    // smartphone GPUs do not support DXT (BC1/2/3/4/5) textures, they must be decompressed on the GPU
    bool support_dxt = false;
    // format for replaced texture, supported mostly by smartphone GPUs
    bool support_astc = false;
    // some smartphone GPUs do not support linear filtering on depth surfaces
    bool support_depth_linear_filtering = true;
    // powerVR only
    bool support_pvrt = false;
    // other color format
    bool support_x8d24 = false;
    bool support_e5rgb9 = false;
    bool support_a2rgb10 = false;

    // Disarms the write traps of an entry that is dropped or reused (the callbacks keep their own state alive).
    static void reset_tracking(TextureCacheInfo &info);

    bool init(const fs::path &texture_folder, const std::string_view game_id, const size_t sampler_cache_size = 0);
    void set_replacement_state(bool import_textures, bool export_textures, bool export_as_png);

    virtual void select(size_t index, const SceGxmTexture &texture) = 0;
    virtual void configure_texture(const SceGxmTexture &texture) = 0;
    virtual void upload_texture_impl(SceGxmTextureBaseFormat base_format, uint32_t width, uint32_t height, uint32_t mip_index, const void *pixels, int face, uint32_t pixels_per_stride) = 0;
    virtual void upload_done() {}
    // Backends that can upload a row range of the first mip (upload_row_offset / partial_upload) keep the other
    // rows of the existing image. Returns false: such uploads are always whole.
    virtual bool supports_partial_upload() const { return false; }
    // True while the GPU can write [begin, end) of guest memory without a CPU write (and so without tripping
    // a write trap): the entries over it are validated by hash on every scene instead of trusting their traps.
    virtual bool gpu_may_write_guest_range(uint32_t begin, uint32_t end) const { return false; }
    // YUV textures: try to convert the first mip on the GPU. Returns true when the whole texture was handled,
    // false to use the CPU conversion. src is the start of the luma plane / packed macro pixels.
    virtual bool upload_yuv_gpu(const SceGxmTexture &texture, SceGxmTextureBaseFormat base_format, const uint8_t *src, uint32_t width, uint32_t height, uint32_t stride_pixels, uint32_t layout_width, uint32_t layout_height) { return false; }

    virtual void configure_sampler(size_t index, const SceGxmTexture &texture, bool no_linear) {}

    // rows: upload only these memory rows of the first mip (partial_upload / upload_row_offset tell the backend)
    void upload_texture(const SceGxmTexture &gxm_texture, MemState &mem, const texture_policy::RowRun *rows = nullptr);
    void cache_and_bind_texture(const SceGxmTexture &gxm_texture, MemState &mem);
    // bind of an entry already in the cache: reads guest memory only when a write trap fired (texture_policy.h)
    texture_policy::UploadKind revalidate_entry(TextureCacheInfo &info, const SceGxmTexture &gxm_texture, MemState &mem, std::vector<texture_policy::RowRun> &runs, bool &hashed);

    // is called by cache_and_bind_texture if use_sampler_cache is set to true
    int cache_and_bind_sampler(const SceGxmTexture &gxm_texture, bool no_linear = false);

    // look at the texture folder and update the available imported / exported hashes
    void refresh_available_textures();

    // functions used for texture exportation
    void export_select(const SceGxmTexture &texture);
    void export_texture_impl(SceGxmTextureBaseFormat base_format, uint32_t width, uint32_t height, uint32_t mip_index, const void *pixels, int face, uint32_t pixels_per_stride);
    void export_done();

    // return false if there was an issue with the replacement texture
    bool import_configure_texture();
    virtual void import_configure_impl(SceGxmTextureBaseFormat base_format, uint32_t width, uint32_t height, bool is_srgb, uint16_t nb_components, uint16_t mipcount, bool swap_rb) = 0;
    void import_upload_texture();
    void import_done();

    virtual ~TextureCache();
};
} // namespace renderer
