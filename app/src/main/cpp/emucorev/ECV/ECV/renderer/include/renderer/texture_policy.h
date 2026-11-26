// Texture cache validation policy.
//
// Header-only and free of Vulkan/guest-memory types on purpose: every rule below is a pure function so it can
// be proven by host unit tests (app/src/test/core-host/texture_policy_tests.cpp); texture/cache.cpp owns the
// side effects (write traps, hashing, uploads).
//
// Model: a cached texture is "clean" while the write traps armed over its guest bytes (and, for P4/P8, over its
// palette) have not fired. A clean entry is used without reading guest memory. A fired trap only says that
// somebody touched the pages: the bytes are then hashed again, and only a hash difference causes an upload.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace renderer::texture_policy {

// ---------------------------------------------------------------------------------------------
// Lookup of a cached entry: what has to be read from guest memory before the entry can be used.
// ---------------------------------------------------------------------------------------------

struct EntryState {
    // replacement/export toggles: the entry must be uploaded again whatever its memory did
    bool forced = false;
    // the entry was already validated for the scene being recorded: one content version per scene
    bool validated_this_scene = false;
    // the pixel write trap is armed and has not fired
    bool pixels_armed = false;
    bool pixels_fired = false;
    // the texture has a palette (P4/P8) with its own write trap
    bool paletted = false;
    bool palette_armed = false;
    bool palette_fired = false;
    // guest memory was released since the trap was armed: a trap cannot see that
    bool memory_released = false;
    // a GPU surface that writes the guest memory directly (no CPU write-back, so no fault) overlaps the texture
    bool gpu_may_write = false;
};

struct Revalidation {
    bool hash_pixels = false;
    bool hash_palette = false;
    // nothing to check: the clean fast path
    bool skip = false;
};

constexpr Revalidation revalidation_needed(const EntryState &s) {
    Revalidation r;
    if (s.forced)
        return r; // forced uploads rebuild every baseline themselves
    if (s.validated_this_scene) {
        r.skip = true;
        return r;
    }
    r.hash_pixels = !s.pixels_armed || s.pixels_fired || s.memory_released || s.gpu_may_write;
    if (s.paletted)
        r.hash_palette = !s.palette_armed || s.palette_fired || s.memory_released || s.gpu_may_write;
    r.skip = !r.hash_pixels && !r.hash_palette;
    return r;
}

// ---------------------------------------------------------------------------------------------
// What to upload once the hashes are known.
// ---------------------------------------------------------------------------------------------

enum class UploadKind : uint8_t {
    None,        // rewritten with identical bytes, or nothing was written: keep the GPU texture
    Full,        // every row of every mip is converted and uploaded again
    PaletteOnly, // only the palette changed: indices are converted again, pixel hashes stay valid
    Partial,     // only the changed row runs of a linear single-mip texture
};

struct HashOutcome {
    bool pixels_checked = false;
    bool pixels_changed = false;
    bool palette_checked = false;
    bool palette_changed = false;
    // the layout is row addressable (see plan_chunks) and the backend can upload a sub-rectangle
    bool partial_possible = false;
    uint32_t dirty_rows = 0;
    uint32_t total_rows = 0;
};

// A partial upload is only cheaper while a good part of the texture is untouched: it costs one copy region
// and one conversion call per run. Above this share of changed rows the whole texture is uploaded.
// PROVISIONAL (no device measurement): the break-even depends on the conversion cost per format.
inline constexpr uint32_t partial_max_dirty_percent = 75;

constexpr UploadKind decide_upload(const HashOutcome &o) {
    const bool pixels = o.pixels_checked && o.pixels_changed;
    const bool palette = o.palette_checked && o.palette_changed;
    if (!pixels && !palette)
        return UploadKind::None;
    if (!pixels)
        return UploadKind::PaletteOnly;
    // a changed palette recolours every pixel
    if (palette || !o.partial_possible || o.total_rows == 0)
        return UploadKind::Full;
    if (static_cast<uint64_t>(o.dirty_rows) * 100 > static_cast<uint64_t>(o.total_rows) * partial_max_dirty_percent)
        return UploadKind::Full;
    return UploadKind::Partial;
}

// ---------------------------------------------------------------------------------------------
// Row chunks of linear textures: the unit of the pixel hash, and so of a partial upload.
// Tiled and swizzled layouts have no row addressable memory (a changed byte maps to a 2D tile or a Morton
// block), they are always hashed as one chunk and uploaded whole.
// ---------------------------------------------------------------------------------------------

// Linear textures smaller than this are hashed as a whole.
inline constexpr uint32_t partial_min_bytes = 64u << 10;
// Target size of one hash chunk (a whole number of block rows, at least one).
inline constexpr uint32_t chunk_target_bytes = 16u << 10;

struct ChunkPlan {
    // rows (pixel rows, a multiple of block_rows) hashed together; 0 = the texture is one chunk
    uint32_t rows_per_chunk = 0;
    uint32_t chunk_count = 1;
};

// row_pitch_bytes: bytes of one block row (block_rows pixel rows); total_rows: pixel rows in memory (a multiple
// of block_rows); row_addressable: linear, single mip, single face.
constexpr ChunkPlan plan_chunks(bool row_addressable, uint32_t texture_bytes, uint32_t row_pitch_bytes, uint32_t total_rows, uint32_t block_rows) {
    ChunkPlan plan;
    if (!row_addressable || texture_bytes < partial_min_bytes || row_pitch_bytes == 0 || block_rows == 0 || total_rows == 0)
        return plan;
    const uint32_t block_row_count = total_rows / block_rows;
    const uint32_t block_rows_per_chunk = std::max<uint32_t>(1, chunk_target_bytes / row_pitch_bytes);
    if (block_rows_per_chunk >= block_row_count)
        return plan;
    plan.rows_per_chunk = block_rows_per_chunk * block_rows;
    plan.chunk_count = (block_row_count + block_rows_per_chunk - 1) / block_rows_per_chunk;
    return plan;
}

struct RowRun {
    uint32_t row_begin = 0;
    uint32_t row_end = 0;
    constexpr bool operator==(const RowRun &) const = default;
};

// Runs of consecutive changed chunks, as pixel rows (end exclusive, clipped to total_rows). Hash vectors of
// a different length (the layout changed) mean everything changed.
inline std::vector<RowRun> changed_runs(const std::vector<uint64_t> &before, const std::vector<uint64_t> &after, const ChunkPlan &plan, uint32_t total_rows) {
    std::vector<RowRun> runs;
    if (before.size() != after.size() || after.size() != plan.chunk_count || plan.rows_per_chunk == 0) {
        runs.push_back({ 0, total_rows });
        return runs;
    }
    for (uint32_t i = 0; i < plan.chunk_count; ++i) {
        if (before[i] == after[i])
            continue;
        const uint32_t begin = i * plan.rows_per_chunk;
        const uint32_t end = std::min(total_rows, begin + plan.rows_per_chunk);
        if (!runs.empty() && runs.back().row_end == begin)
            runs.back().row_end = end;
        else
            runs.push_back({ begin, end });
    }
    return runs;
}

inline uint32_t rows_in_runs(const std::vector<RowRun> &runs) {
    uint32_t rows = 0;
    for (const RowRun &r : runs)
        rows += r.row_end - r.row_begin;
    return rows;
}

// ---------------------------------------------------------------------------------------------
// Trap span: whole host pages covering the bytes (faults from unrelated data sharing a page are judged by hash).
// ---------------------------------------------------------------------------------------------

struct PageSpan {
    uint32_t begin = 0;
    uint32_t end = 0;
};

constexpr PageSpan trap_span(uint32_t address, uint32_t bytes, uint32_t page_size) {
    PageSpan s;
    s.begin = address - address % page_size;
    const uint64_t end = static_cast<uint64_t>(address) + bytes;
    s.end = static_cast<uint32_t>(std::min<uint64_t>(0xFFFFFFFFull, (end + page_size - 1) / page_size * page_size));
    return s;
}

} // namespace renderer::texture_policy
