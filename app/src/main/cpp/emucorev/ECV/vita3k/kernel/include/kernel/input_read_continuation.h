// Input reads share the display wait operation in existing save archives.
#pragma once

#include <array>
#include <cpu/common.h>
#include <cstdint>
#include <limits>

inline bool is_touch_read_import(uint32_t nid) {
    return nid == 0x169A1D58U || nid == 0x39401BEAU;
}

inline bool is_ctrl_read_import(uint32_t nid) {
    switch (nid) {
    case 0x15F96FB0U:
    case 0x27A0C5FBU:
    case 0x67E7AB83U:
    case 0xC4226A3EU:
    case 0xE2D99296U:
    case 0xA7178860U: return true;
    default: return false;
    }
}

// Match the complete ARM import stub emitted by the loader. A NID alone is
// insufficient: ordinary guest code may contain the same scalar value.
template <typename Read>
uint32_t input_read_import_at_wait(const CPUContext &context, const Read &read) {
    const uint32_t pc = context.get_pc();
    if (context.thumb() || (pc & 3) || pc < 4 || pc > std::numeric_limits<uint32_t>::max() - 7)
        return 0;
    std::array<uint32_t, 3> stub{};
    if (!read(uint64_t{ pc } - 4, stub.data(), sizeof(stub))
        || stub[0] != 0xef000000U || stub[1] != 0xe1a0f00eU)
        return 0;
    return is_touch_read_import(stub[2]) || is_ctrl_read_import(stub[2]) ? stub[2] : 0;
}

inline thread_local bool input_read_vblank_completed = false;

inline bool input_read_resuming_after_vblank() {
    return input_read_vblank_completed;
}

// Another reader may have advanced the shared port cursor while this syscall
// waited. Its restored completion must not enroll in a second vblank wait.
class InputReadResumeScope {
    bool previous = input_read_vblank_completed;

public:
    InputReadResumeScope() { input_read_vblank_completed = true; }
    ~InputReadResumeScope() { input_read_vblank_completed = previous; }
    InputReadResumeScope(const InputReadResumeScope &) = delete;
    InputReadResumeScope &operator=(const InputReadResumeScope &) = delete;
};
