// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <mem/util.h>
struct KernelState;
struct AudioState;
struct MemState;
struct ThreadState;
void register_audio_wait_handlers(KernelState &kernel, AudioState &audio, MemState &mem);
int32_t audio_output_wait(KernelState &kernel, AudioState &audio, MemState &mem,
    ThreadState &thread, int port, Address buffer);

#include <functional>
#include <string>
struct WaitContinuationSnapshot;
struct AudioWaitConfiguration {
    int id, len, len_bytes, freq, mode;
    uint64_t len_microseconds;
    uint32_t codec;
};
// Pure staged-state validation; never reads current guest RAM or host queues.
bool validate_audio_wait_configuration(const WaitContinuationSnapshot &wait,
    const AudioWaitConfiguration &port, const std::function<bool(Address, size_t)> &contains,
    std::string &error);
