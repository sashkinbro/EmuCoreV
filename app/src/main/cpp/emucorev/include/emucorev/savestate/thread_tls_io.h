#pragma once

// Optional save-state section (SectionId::ThreadTls): per-thread TPIDRURW, the user read/write thread ID
// register. It lives outside CPUContext and the Threads section so engine 20 saves stay valid: a state
// without the section restores every thread with TPIDRURW 0, which is what such saves always meant.
//
// Payload: u32 count, then count x (u32 thread id, u32 value). Zero values are not written.

#include <emucorev/savestate/state_io.h>

#include <cstdint>
#include <string>
#include <vector>

namespace emucorev::savestate {

struct ThreadTlsEntry {
    uint32_t thread_id = 0;
    uint32_t tpidrurw = 0;
};

inline void encode_thread_tpidrurw(BufferWriter &buffer, const std::vector<ThreadTlsEntry> &entries) {
    uint32_t count = 0;
    for (const ThreadTlsEntry &entry : entries)
        count += entry.tpidrurw != 0;
    buffer.u32(count);
    for (const ThreadTlsEntry &entry : entries) {
        if (entry.tpidrurw == 0)
            continue;
        buffer.u32(entry.thread_id);
        buffer.u32(entry.tpidrurw);
    }
}

// An empty payload is the absent section: no entries, success.
inline bool parse_thread_tpidrurw(const std::vector<uint8_t> &data, std::vector<ThreadTlsEntry> &out, std::string &error) {
    out.clear();
    if (data.empty())
        return true;
    BufferReader reader(data.data(), data.size());
    const uint32_t count = reader.u32();
    if (!reader.ok() || static_cast<uint64_t>(count) * 8 != reader.remaining()) {
        error = "invalid thread TLS section";
        return false;
    }
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        ThreadTlsEntry entry;
        entry.thread_id = reader.u32();
        entry.tpidrurw = reader.u32();
        out.push_back(entry);
    }
    return true;
}

} // namespace emucorev::savestate
