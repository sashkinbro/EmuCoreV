// EmuCoreV: bookkeeping of USSE buffer stores on the route without memory mapping (build-out of C5).
//
// Pure logic, no Vulkan calls, so that it can be unit tested on the host.
//
// Without memory mapping a shader sees the guest buffers as a storage-buffer copy: every draw uploads the uniform blocks it binds into its own
// region of the (host visible) vertex / fragment stream ring buffer and the shader indexes that copy. A store instruction writes the copy, so the
// renderer has to give the written bytes back to guest memory. Per draw it remembers which guest range each block of the region came from and the
// guest bytes at upload time; after the scene's fence it compares the region with those bytes and writes only the bytes the shader changed
// (a draw that uploaded a stale copy must not undo what an earlier draw of the scene stored).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace renderer::vulkan::buffer_store {

enum Stage : uint8_t {
    Vertex = 1,
    Fragment = 2,
};

// A uniform block of one draw: where it came from in guest memory and where its copy lives in the stage's ring buffer.
struct Block {
    uint32_t guest_address = 0;
    uint32_t size = 0; // bytes
    uint32_t ring_offset = 0; // bytes: relative to the draw's region while it is recorded, absolute once collected
    uint32_t ring_wraps = 0; // ring wrap counter at the time the region was allocated (collected blocks only)
    uint8_t stage = Vertex;
    int32_t block_num = 0;
};

// The guest binds one buffer per block number and stage: setting a block again within a draw replaces the earlier upload.
inline void note_block(std::vector<Block> &blocks, const Block &block) {
    for (Block &existing : blocks) {
        if (existing.stage == block.stage && existing.block_num == block.block_num) {
            existing = block;
            return;
        }
    }
    blocks.push_back(block);
}

// Is a ring region still what the draw left there? The ring wraps (a later draw overwrites the oldest regions): the region survives when
// the ring has not wrapped since, or it wrapped once and the write cursor has not yet reached the region.
inline bool ring_data_intact(const uint32_t wraps_at, const uint32_t offset, const uint32_t size, const uint32_t wraps_now, const uint32_t cursor_now) {
    if (wraps_at == wraps_now)
        return true;
    return wraps_now == wraps_at + 1 && cursor_now <= offset && size != 0;
}

inline bool range_in_buffer(const uint64_t buffer_size, const uint32_t offset, const uint32_t size) {
    return size != 0 && static_cast<uint64_t>(offset) + size <= buffer_size;
}

// A run of bytes that differ between the uploaded copy and the copy after the shader ran.
struct Run {
    uint32_t offset;
    uint32_t length;
};

// Bytes closer than `merge_gap` are written as one run (fewer, larger guest writes); the gap bytes are equal in both copies so writing them is
// a no-op for the guest.
inline std::vector<Run> changed_runs(const uint8_t *before, const uint8_t *after, const size_t size, const uint32_t merge_gap = 0) {
    std::vector<Run> runs;
    size_t i = 0;
    while (i < size) {
        if (before[i] == after[i]) {
            i++;
            continue;
        }
        const size_t first = i;
        size_t last = i; // last differing byte
        i++;
        while (i < size && i - last <= static_cast<size_t>(merge_gap) + 1) {
            if (before[i] != after[i])
                last = i;
            i++;
        }
        runs.push_back({ static_cast<uint32_t>(first), static_cast<uint32_t>(last - first + 1) });
        i = last + 1;
    }
    return runs;
}

// Does a SPIR-V module write to a storage buffer variable? The translator declares the guest buffer container NonWritable unless the shader
// stores into it, and without memory mapping it is the only StorageBuffer variable, so this decides from the module itself (a cached module has
// no translation statistics). Modules with a malformed header answer false.
inline bool spirv_has_writable_storage_buffer(const uint32_t *words, const size_t count) {
    constexpr uint32_t spirv_magic = 0x07230203u;
    constexpr uint32_t op_decorate = 71, op_variable = 59;
    constexpr uint32_t decoration_non_writable = 24, storage_class_storage_buffer = 12;
    if (words == nullptr || count < 5 || words[0] != spirv_magic)
        return false;
    std::vector<uint32_t> non_writable, storage_buffers;
    for (size_t i = 5; i < count;) {
        const uint32_t word_count = words[i] >> 16, opcode = words[i] & 0xFFFFu;
        if (word_count == 0 || i + word_count > count)
            return false;
        if (opcode == op_decorate && word_count >= 3 && words[i + 2] == decoration_non_writable)
            non_writable.push_back(words[i + 1]);
        else if (opcode == op_variable && word_count >= 4 && words[i + 3] == storage_class_storage_buffer)
            storage_buffers.push_back(words[i + 2]);
        i += word_count;
    }
    for (const uint32_t id : storage_buffers) {
        bool read_only = false;
        for (const uint32_t decorated : non_writable)
            read_only |= decorated == id;
        if (!read_only)
            return true;
    }
    return false;
}

} // namespace renderer::vulkan::buffer_store
