// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cpu/functions.h>
#include <kernel/state.h>
#include <kernel/thread/thread_state.h>
#include <util/log.h>

#include <algorithm>
#include <cassert>
#include <cstring>

void ThreadState::push_arguments(const std::vector<uint32_t> &args) {
    Address sp = read_sp(*cpu);
    for (size_t i = 0; i < std::min(args.size(), static_cast<size_t>(4)); i++) {
        write_reg(*cpu, i, args[i]);
    }
    if (args.size() > 4) {
        // TODO align to 16 bytes
        const size_t remain_size = args.size() - 4;
        sp -= 4 * remain_size;
        memcpy(Ptr<uint32_t>(sp).get(mem), &args[4], remain_size * 4);
    }
    write_sp(*cpu, sp);
}

uint32_t ThreadState::run_callback_locked(std::unique_lock<std::mutex> &thread_lock, Address callback_address, const std::vector<uint32_t> &args, CallbackPurpose purpose, SceUID callback_uid, std::array<uint32_t, 4> completion, uint32_t external_tag) {
    assert(thread_lock.owns_lock());
    if (call_level == 0) {
        LOG_ERROR("run_callback should not be called as the first thread entry");
        return 0;
    }

    // save the current context before overwriting PC/LR for the callback
    auto frame = std::make_shared<CallbackContinuationSnapshot>();
    frame->frame_sequence = next_continuation_sequence++;
    frame->previous_context = save_context(*cpu);
    frame->previous_tpidruro = read_tpidruro(*cpu);
    frame->purpose = purpose;
    frame->callback_uid = callback_uid;
    frame->completion = completion;
    frame->external_tag = external_tag;
    callback_frames.push_back(frame);

    // we shouldn't have to clean the context I believe
    write_pc(*cpu, callback_address);
    write_lr(*cpu, kernel.halt_instruction_pc);
    push_arguments(args);
    thread_lock.unlock();

    // unlock but then immediately lock back in the run_loop function
    // shouldn't cause an issue, but maybe we could use a recursive mutex instead
    run_loop();

    thread_lock.lock();
    frame->guest_returned = true;
    frame->result = returned_value;
    // The nested frame may have parked while a freeze was accepted. Context
    // restoration is a guest-context mutation too, and must wait for resume.
    if (!wait_for_guest_resume(thread_lock))
        return returned_value;

    // restore the previous context
    // actually, in most case I don't think this is necessary as the caller
    // and the callee should respect the same calling convention
    // but do it just in case
    load_context(*cpu, frame->previous_context);
    write_tpidruro(*cpu, frame->previous_tpidruro);
    std::erase(callback_frames, frame);

    return returned_value;
}
