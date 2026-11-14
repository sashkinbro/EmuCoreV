// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cpu/common.h>
#include <kernel/thread/wait.h>
#include <memory>
#include <mutex>

// Logical HLE state only. Guest addresses remain addresses across RAM replacement;
// host stack addresses, condition variables and steady-clock epochs never persist.
enum class WaitOperation : uint8_t {
    none,
    delay,
    signal,
    thread_end,
    event,
    timer,
    mutex,
    lw_mutex,
    rw_read,
    rw_write,
    semaphore,
    cond,
    lw_cond,
    event_flag,
    pipe_send,
    pipe_recv,
    creation_import,
    display_vblank,
    audio_output,
    callback_dispatch,
    gxm_display_queue,
    renderer_finish
};
enum class WaitPhase : uint8_t { queued,
    reacquire_mutex,
    complete };
struct WaitContinuationSnapshot {
    uint64_t frame_sequence = 0;
    WaitOperation operation = WaitOperation::none;
    WaitPhase phase = WaitPhase::queued;
    std::array<uint32_t, 8> args{};
    Address timeout_address = 0;
    bool callbacks = false;
    bool infinite = true;
    uint64_t remaining_us = 0;
    CPUContext context;
    WaitTarget target;
    int priority = 0;
    uint64_t sequence = 0;
    bool queued = false;
    bool has_result = false;
    SceInt32 result = 0;
    SceInt32 condition_result = 0;
};
struct WaitContinuation {
    std::mutex mutex;
    WaitContinuationSnapshot state;
    Deadline deadline = Deadline::max();
    bool resuming = false;
    std::shared_ptr<void> object;
    std::shared_ptr<void> associated_object;
    // Only used during reconstruction, never serialized. Cleared when the
    // resumed helper takes ownership, or when a deleted host worker unwinds.
    std::shared_ptr<void> restored_node;
    std::function<void()> detach_restored_waiter;
};

enum class CallbackPurpose : uint8_t { direct,
    notification,
    thread_start,
    thread_end,
    abort_handler,
    external };
struct CallbackContinuationSnapshot {
    uint64_t frame_sequence = 0;
    CPUContext previous_context;
    uint32_t previous_tpidruro = 0;
    CallbackPurpose purpose = CallbackPurpose::direct;
    SceUID callback_uid = 0;
    bool guest_returned = false;
    uint32_t result = 0;
    // Module-specific callers use a stable tag and logical completion payload;
    // they must register a matching resume handler rather than retain host locals.
    uint32_t external_tag = 0;
    std::array<uint32_t, 4> completion{};
};

// nids/nids.inc: these exports perform no allocation before create_thread admission.
// The LibKernel wrapper uses temporary guest stack space, so replay uses the
// context captured before entering the import, rather than its parked context.
inline bool restartable_creation_import(uint32_t nid) {
    return nid == 0xC0FAF6A3U || nid == 0xC6674E7DU || nid == 0xC5C11EE7U;
}

inline constexpr uint32_t check_callback_import = 0xE53E41F6U;
inline bool restartable_callback_import(uint32_t nid) {
    switch (nid) {
    case check_callback_import:
    // Threadmgr CB entry points and their LibKernel wrappers (nids/nids.inc).
    case 0xDB9F5333U:
    case 0x2BDAA524U:
    case 0x5D86D763U:
    case 0x2D4A62B7U:
    case 0xDBD09B09U:
    case 0xA4777082U:
    case 0x452B0AB3U:
    case 0x4CE42CE2U:
    case 0x7D483C33U:
    case 0xA0490795U:
    case 0x401E0C68U:
    case 0xE737B1DFU:
    case 0x72DBB96BU:
    case 0x8FA54B07U:
    case 0xF8E06784U:
    case 0x174692B4U:
    case 0xCEA3FC52U:
    case 0x24460BB3U:
    case 0xFA3D4491U:
    case 0xC54941EDU:
    case 0x3148C6B6U:
    case 0x33AF829BU:
    case 0xA5CA74ACU:
    // Display initial callback dispatch occurs before computing its vblank target.
    case 0x814C90AFU:
    case 0x3E796EF5U:
    case 0x78B41B92U:
    case 0x05F27764U:
        return true;
    default: return false;
    }
}
