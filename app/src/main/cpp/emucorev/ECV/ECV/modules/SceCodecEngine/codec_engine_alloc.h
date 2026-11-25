// Allocation rules of the codec engine unmap memory blocks (sceCodecEngineAllocMemoryFromUnmapMemBlock /
// FreeMemoryFromUnmapMemBlock), kept free of kernel types so that they can be unit tested on the host.
#pragma once

#include <cstdint>

namespace codec_engine {

// An unmap memory block is invisible to the CPU: the codec engine reaches it through its own mapping, so the
// address handed back by the allocator is a device-side address, never dereferenced by the guest. It is kept
// positive (callers test the int result for < 0) and distinct per block.
constexpr uint32_t DEVICE_BASE = 0x00100000;
constexpr uint32_t DEVICE_STRIDE = 0x03000000;

enum class AllocStatus {
    Ok,
    InvalidSize,
    InvalidValue
};

struct AllocResult {
    AllocStatus status;
    uint32_t vaddr; // device address, valid when status == Ok
};

constexpr uint32_t device_base(int32_t uid) {
    return DEVICE_BASE + (static_cast<uint32_t>(uid) & 0x1F) * DEVICE_STRIDE;
}

// Bump allocation inside a block of block_size bytes; `cursor` is the next free offset and is advanced.
// A block opened with size 0 (rejected by the real engine, never seen so far) is not bounded.
inline AllocResult alloc(int32_t uid, uint32_t block_size, int32_t &cursor, uint32_t size, uint32_t alignment) {
    if (size == 0)
        return { AllocStatus::InvalidSize, 0 };
    if (alignment == 0)
        alignment = 1;
    if (alignment & (alignment - 1))
        return { AllocStatus::InvalidValue, 0 };

    const uint64_t offset = (static_cast<uint64_t>(static_cast<uint32_t>(cursor)) + alignment - 1) & ~static_cast<uint64_t>(alignment - 1);
    // PROVISIONAL: the real error for an exhausted block is not documented, InvalidSize is the closest known code.
    if (block_size != 0 && offset + size > block_size)
        return { AllocStatus::InvalidSize, 0 };

    cursor = static_cast<int32_t>(offset + size);
    return { AllocStatus::Ok, device_base(uid) + static_cast<uint32_t>(offset) };
}

// True when vaddr lies inside the block (the only check a bump allocator can make on a free).
constexpr bool owns(int32_t uid, uint32_t block_size, uint32_t vaddr) {
    const uint32_t base = device_base(uid);
    return vaddr >= base && (block_size == 0 || vaddr - base < block_size);
}

} // namespace codec_engine
