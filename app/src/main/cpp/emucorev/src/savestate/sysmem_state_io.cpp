#include <emucorev/savestate/sysmem_state_io.h>
#include <emucorev/savestate/state_io.h>
#include <emucorev/savestate/memory_image.h>
#include <cstring>
#include <limits>
namespace emucorev::savestate {
bool parse_sysmem_state(const std::vector<uint8_t> &data, SysmemSnapshot &output, std::string &error) {
    if (data.size() > 16u * 1024u * 1024u) { error = "sysmem state exceeds limit"; return false; }
    BufferReader reader(data.data(), data.size());
    SysmemSnapshot state;
    state.next_uid = reader.i32();
    state.allocated_user = reader.u32();
    state.allocated_cdram = reader.u32();
    state.allocated_phycont = reader.u32();
    const auto read = [&](auto &blocks) {
        const uint32_t count = reader.u32();
        if (!reader.ok() || count > 65536 || count > reader.remaining() / (4 + sizeof(KernelMemBlock))) return false;
        for (uint32_t i = 0; i < count; ++i) {
            const SceUID uid = reader.i32();
            const auto block = reader.value<KernelMemBlock>();
            if (!reader.ok() || uid <= 0 || uid >= state.next_uid || !blocks.emplace(uid, block).second) return false;
        }
        return true;
    };
    if (state.next_uid <= 0 || state.next_uid == std::numeric_limits<SceUID>::max()
        || !read(state.blocks) || !read(state.vm_blocks) || !reader.ok() || reader.remaining()) {
        error = "invalid sysmem block table"; return false;
    }
    output = std::move(state);
    return true;
}
bool validate_sysmem_state(const SysmemSnapshot &state, const MemoryImage &memory, std::string &error) {
    const auto fail = [&] { error = "invalid saved sysmem allocation ownership"; return false; };
    std::map<uint64_t, uint64_t> ranges;
    for (const auto &[uid, block] : state.blocks) {
        const uint64_t begin = block.mappedBase.address(), end = begin + block.mappedSize;
        if (uid <= 0 || uid >= state.next_uid || block.size != sizeof(SceKernelMemBlockInfo)
            || !begin || (begin & 4095) || !block.mappedSize || !memory.contains(begin, block.mappedSize)
            || !std::memchr(block.name, 0, sizeof(block.name))) return fail();
        const auto next = ranges.lower_bound(begin);
        if ((next != ranges.end() && next->first < end) || (next != ranges.begin() && std::prev(next)->second > begin)) return fail();
        ranges.emplace(begin, end);
    }
    for (const auto &[uid, block] : state.vm_blocks) {
        const auto owner = state.blocks.find(uid);
        if (owner == state.blocks.end() || std::memcmp(&owner->second, &block, sizeof(block))) return fail();
    }
    return true;
}
}
